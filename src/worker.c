#include <postgres.h>
#include <pgstat.h>
#include <postmaster/bgworker.h>
#include <storage/ipc.h>
#include <storage/latch.h>
#include <storage/proc.h>
#include <fmgr.h>
#include <access/xact.h>
#include <executor/spi.h>
#include <utils/snapmgr.h>
#include <commands/extension.h>
#include <catalog/pg_extension.h>
#include <catalog/pg_type.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <access/hash.h>
#include <utils/hsearch.h>
#include <utils/memutils.h>
#include <utils/jsonb.h>
#include <utils/guc.h>
#include <tcop/utility.h>

#include <curl/curl.h>
#include <curl/multi.h>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include "util.h"
#include "core.h"

#define MIN_LIBCURL_VERSION_NUM 0x075300 // This is the 7.83.0 version in hex as defined in curl/curlver.h
#define REQUIRED_LIBCURL_ERR_MSG "libcurl >= 7.83.0 is required, we use the curl_easy_nextheader() function added in this version"
_Static_assert(LIBCURL_VERSION_NUM, REQUIRED_LIBCURL_ERR_MSG); // test for older libcurl versions that don't even have LIBCURL_VERSION_NUM defined (e.g. libcurl 6.5).
_Static_assert(LIBCURL_VERSION_NUM >= MIN_LIBCURL_VERSION_NUM, REQUIRED_LIBCURL_ERR_MSG);

PG_MODULE_MAGIC;

static char *guc_ttl;
static int guc_batch_size;
static char* guc_database_name;
static MemoryContext CurlMemContext = NULL;

void _PG_init(void);
PGDLLEXPORT void pg_net_worker(Datum main_arg) pg_attribute_noreturn();

static long latch_timeout = 1000;
static volatile sig_atomic_t got_sigterm = false;
static volatile sig_atomic_t got_sighup = false;

static void
handle_sigterm(SIGNAL_ARGS)
{
  int save_errno = errno;
  got_sigterm = true;
  if (MyProc)
    SetLatch(&MyProc->procLatch);
  errno = save_errno;
}

static void
handle_sighup(SIGNAL_ARGS)
{
  int     save_errno = errno;
  got_sighup = true;
  if (MyProc)
    SetLatch(&MyProc->procLatch);
  errno = save_errno;
}

static bool is_extension_loaded(){
  Oid extensionOid;

  StartTransactionCommand();

  extensionOid = get_extension_oid("pg_net", true);

  CommitTransactionCommand();

  return OidIsValid(extensionOid);
}

void pg_net_worker(Datum main_arg) {
  pqsignal(SIGTERM, handle_sigterm);
  pqsignal(SIGHUP, handle_sighup);
  pqsignal(SIGUSR1, procsignal_sigusr1_handler);

  BackgroundWorkerUnblockSignals();

  BackgroundWorkerInitializeConnection(guc_database_name, NULL, 0);

  elog(INFO, "pg_net_worker started with a config of: pg_net.ttl=%s, pg_net.batch_size=%d, pg_net.database_name=%s", guc_ttl, guc_batch_size, guc_database_name);

  int curl_ret = curl_global_init(CURL_GLOBAL_ALL);
  if(curl_ret != CURLE_OK)
    ereport(ERROR, errmsg("curl_global_init() returned %s\n", curl_easy_strerror(curl_ret)));

  LoopState lstate = {
    .epfd = epoll_create1(0),
    .timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC),
    .curl_mhandle = curl_multi_init(),
  };

  if (lstate.epfd < 0) {
    ereport(ERROR, errmsg("Failed to create epoll file descriptor"));
  }

  if (lstate.timerfd < 0) {
    ereport(ERROR, errmsg("Failed to create timerfd"));
  }

  if(!lstate.curl_mhandle)
    ereport(ERROR, errmsg("curl_multi_init()"));

  set_curl_mhandle(lstate.curl_mhandle, &lstate);

  timerfd_settime(lstate.timerfd, 0, &(itimerspec){}, NULL);

  epoll_ctl(lstate.epfd, EPOLL_CTL_ADD, lstate.timerfd, &(epoll_event){.events = EPOLLIN, .data.fd = lstate.timerfd});

  while (!got_sigterm) {
    WaitLatch(&MyProc->procLatch,
          WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
          latch_timeout,
          PG_WAIT_EXTENSION);
    ResetLatch(&MyProc->procLatch);

    CHECK_FOR_INTERRUPTS();

    if(!is_extension_loaded()){
      elog(DEBUG2, "pg_net_worker: extension not yet loaded");
      continue;
    }

    if (got_sighup) {
      got_sighup = false;
      ProcessConfigFile(PGC_SIGHUP);
    }

    delete_expired_responses(guc_ttl, guc_batch_size);

    consume_request_queue(lstate.curl_mhandle, guc_batch_size, CurlMemContext);

    int running_handles = 0;
    int maxevents = guc_batch_size + 1; // 1 extra for the timer
    epoll_event *events = palloc0(sizeof(epoll_event) * maxevents);

    do {
      int nfds = epoll_wait(lstate.epfd, events, maxevents, /*timeout=*/1000);
      if (nfds < 0) {
        int save_errno = errno;
        if(save_errno == EINTR) { // can happen when the epoll is interrupted, for example when running under GDB. Just continue in this case.
          continue;
        }
        else {
          ereport(ERROR, errmsg("epoll_wait() failed: %s", strerror(save_errno)));
          break;
        }
      }

      for (int i = 0; i < nfds; i++) {
        if (events[i].data.fd == lstate.timerfd) {
          EREPORT_MULTI(
            curl_multi_socket_action(lstate.curl_mhandle, CURL_SOCKET_TIMEOUT, 0, &running_handles)
          );
        } else {
          int ev_bitmask =
            events[i].events & EPOLLIN ? CURL_CSELECT_IN:
            events[i].events & EPOLLOUT ? CURL_CSELECT_OUT:
            CURL_CSELECT_ERR;

          EREPORT_MULTI(
            curl_multi_socket_action(
              lstate.curl_mhandle, events[i].data.fd,
              ev_bitmask,
              &running_handles)
          );

          if(running_handles <= 0) {
            elog(DEBUG2, "last transfer done, kill timeout");
            timerfd_settime(lstate.timerfd, 0, &(itimerspec){0}, NULL);
          }
        }

        insert_curl_responses(&lstate, CurlMemContext);
      }

    } while (running_handles > 0); // run again while there are curl handles, this will prevent waiting for the latch_timeout (which will cause the cause the curl timeouts to be wrong)

    pfree(events);

    MemoryContextReset(CurlMemContext);
  }

  close(lstate.epfd);
  close(lstate.timerfd);

  curl_multi_cleanup(lstate.curl_mhandle);
  curl_global_cleanup();

  // causing a failure on exit will make the postmaster process restart the bg worker
  proc_exit(EXIT_FAILURE);
}

void _PG_init(void) {
  if (IsBinaryUpgrade) {
      return;
  }

  if (!process_shared_preload_libraries_in_progress) {
      ereport(ERROR, errmsg("pg_net is not in shared_preload_libraries"),
              errhint("Add pg_net to the shared_preload_libraries "
                      "configuration variable in postgresql.conf."));
  }

  RegisterBackgroundWorker(&(BackgroundWorker){
    .bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION,
    .bgw_start_time = BgWorkerStart_RecoveryFinished,
    .bgw_library_name = "pg_net",
    .bgw_function_name = "pg_net_worker",
    .bgw_name = "pg_net " EXTVERSION " worker",
    .bgw_restart_time = 1,
  });

  CurlMemContext = AllocSetContextCreate(TopMemoryContext,
                       "pg_net curl context",
                       ALLOCSET_DEFAULT_MINSIZE,
                       ALLOCSET_DEFAULT_INITSIZE,
                       ALLOCSET_DEFAULT_MAXSIZE);

  DefineCustomStringVariable("pg_net.ttl",
                 "time to live for request/response rows",
                 "should be a valid interval type",
                 &guc_ttl,
                 "6 hours",
                 PGC_SUSET, 0,
                 NULL, NULL, NULL);

  DefineCustomIntVariable("pg_net.batch_size",
                 "number of requests executed in one iteration of the background worker",
                 NULL,
                 &guc_batch_size,
                 200,
                 0, PG_INT16_MAX,
                 PGC_SUSET, 0,
                 NULL, NULL, NULL);

  DefineCustomStringVariable("pg_net.database_name",
                "Database where pg_net tables are located",
                NULL,
                &guc_database_name,
                "postgres",
                PGC_SIGHUP, 0,
                NULL, NULL, NULL);
}
