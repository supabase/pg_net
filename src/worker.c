#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#define PG_PRELUDE_IMPL
#include "pg_prelude.h"
#include "curl_prelude.h"
#include "util.h"
#include "errors.h"
#include "core.h"
#include "event.h"

#define MIN_LIBCURL_VERSION_NUM 0x075300 // This is the 7.83.0 version in hex as defined in curl/curlver.h
#define REQUIRED_LIBCURL_ERR_MSG "libcurl >= 7.83.0 is required, we use the curl_easy_nextheader() function added in this version"
_Static_assert(LIBCURL_VERSION_NUM, REQUIRED_LIBCURL_ERR_MSG); // test for older libcurl versions that don't even have LIBCURL_VERSION_NUM defined (e.g. libcurl 6.5).
_Static_assert(LIBCURL_VERSION_NUM >= MIN_LIBCURL_VERSION_NUM, REQUIRED_LIBCURL_ERR_MSG);

PG_MODULE_MAGIC;

typedef enum {
  WORKER_WAIT_NO_TIMEOUT,
  WORKER_WAIT_ONE_SECOND,
} WorkerWait;

static WorkerState *worker_state = NULL;

static const int                curl_handle_event_timeout_ms = 1000;
static const int                net_worker_restart_time_sec = 1;
static const long               no_timeout = -1L;
static bool                     extension_locked = false;
static bool                     wake_commit_cb_active = false;
static bool                     worker_should_restart = false;

static char*                    guc_ttl;
static int                      guc_batch_size;
static char*                    guc_database_name;
static char*                    guc_username;
static MemoryContext            CurlMemContext = NULL;
static LockRelId                queue_table_lock;
static LockRelId                response_table_lock;
static shmem_startup_hook_type  prev_shmem_startup_hook = NULL;
static volatile sig_atomic_t    got_sighup = false;

void _PG_init(void);

#if PG_VERSION_NUM >= 180000
  PGDLLEXPORT pg_noreturn void pg_net_worker(Datum main_arg);
#else
  PGDLLEXPORT void pg_net_worker(Datum main_arg) pg_attribute_noreturn();
#endif

PG_FUNCTION_INFO_V1(worker_restart);
Datum worker_restart(__attribute__ ((unused)) PG_FUNCTION_ARGS) {
  bool result = DatumGetBool(DirectFunctionCall1(pg_reload_conf, (Datum) NULL)); // reload the config
  pg_atomic_write_u32(&worker_state->got_restart, 1);
  pg_write_barrier();
  if (worker_state)
    SetLatch(&worker_state->latch);
  PG_RETURN_BOOL(result); // TODO is not necessary to return a bool here, but we do it to maintain backward compatibility
}

static void wait_until_state(WorkerState *ws, WorkerStatus expected_status){
  if (pg_atomic_read_u32(&ws->status) == expected_status) // fast return without sleeping, in case condition is fulfilled
    return;

  ConditionVariablePrepareToSleep(&ws->cv);
  while (pg_atomic_read_u32(&ws->status) != expected_status) {
    ConditionVariableSleep(&ws->cv, PG_WAIT_EXTENSION);
  }
  ConditionVariableCancelSleep();
}

PG_FUNCTION_INFO_V1(wait_until_running);
Datum wait_until_running(__attribute__ ((unused)) PG_FUNCTION_ARGS){
  wait_until_state(worker_state, WS_RUNNING);

  PG_RETURN_VOID();
}

// only wake at commit time to prevent excessive and unnecessary wakes.
// e.g only one wake when doing `select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,100000);`
static void wake_at_commit(XactEvent event, __attribute__ ((unused)) void *arg){
  elog(DEBUG2, "pg_net xact callback received: %s", xact_event_name(event));

  switch(event){
    case XACT_EVENT_COMMIT:
    case XACT_EVENT_PARALLEL_COMMIT:
      if(wake_commit_cb_active){
        uint32 expected = 0;
        bool success = pg_atomic_compare_exchange_u32(&worker_state->should_wake, &expected, 1);
        pg_write_barrier();

        if (success) // only wake the worker on first put, so if many concurrent wakes come we only wake once
          SetLatch(&worker_state->latch);

        wake_commit_cb_active = false;
      }
      break;
    // TODO: `PREPARE TRANSACTION 'xx';` and `COMMIT PREPARED TRANSACTION 'xx';` do not wake the worker automatically, they require a manual `net.wake()`
    // These are disabled by default and rarely used, see `max_prepared_transactions` https://www.postgresql.org/docs/17/runtime-config-resource.html#GUC-MAX-PREPARED-TRANSACTIONS
    case XACT_EVENT_PREPARE:
    // abort the callback on rollback
    case XACT_EVENT_ABORT:
    case XACT_EVENT_PARALLEL_ABORT:
      wake_commit_cb_active = false;
      break;
    default:
      break;
  }
}

PG_FUNCTION_INFO_V1(wake);
Datum wake(__attribute__ ((unused)) PG_FUNCTION_ARGS) {
  if (!wake_commit_cb_active) { // register only one callback per transaction
    RegisterXactCallback(wake_at_commit, NULL);
    wake_commit_cb_active = true;
  }

  PG_RETURN_VOID();
}

static void
handle_sigterm(__attribute__ ((unused)) SIGNAL_ARGS)
{
  int save_errno = errno;
  pg_atomic_write_u32(&worker_state->got_restart, 1);
  pg_write_barrier();
  if (worker_state)
    SetLatch(&worker_state->latch);
  errno = save_errno;
}

static void
handle_sighup(__attribute__ ((unused)) SIGNAL_ARGS)
{
  int     save_errno = errno;
  got_sighup = true;
  if (worker_state)
    SetLatch(&worker_state->latch);
  errno = save_errno;
}

static void publish_state(WorkerStatus s) {
  pg_atomic_write_u32(&worker_state->status, (uint32)s);
  pg_write_barrier();
  ConditionVariableBroadcast(&worker_state->cv);
}

static bool is_extension_loaded(){
  StartTransactionCommand();

  bool extension_exists = OidIsValid(get_extension_oid("pg_net", true));

  if(extension_exists && !extension_locked){
    Oid db_oid = get_database_oid(guc_database_name, false);

    Oid net_oid = get_namespace_oid("net", false);

    queue_table_lock.dbId = db_oid;
    queue_table_lock.relId = get_relname_relid("http_request_queue", net_oid);

    response_table_lock.dbId = db_oid;
    response_table_lock.relId = get_relname_relid("_http_response", net_oid);
  }

  CommitTransactionCommand();

  return extension_exists;
}

static inline void lock_extension(){
  if(!extension_locked){
    elog(DEBUG1, "pg_net worker locking extension tables");
    LockRelationIdForSession(&queue_table_lock, AccessShareLock);
    LockRelationIdForSession(&response_table_lock, AccessShareLock);
    extension_locked = true;
  }
}

static inline void unlock_extension(){
  if(extension_locked){
    elog(DEBUG1, "pg_net worker unlocking extension tables");
    UnlockRelationIdForSession(&queue_table_lock, AccessShareLock);
    UnlockRelationIdForSession(&response_table_lock, AccessShareLock);
    extension_locked = false;
  }
}

static void
net_on_exit(__attribute__ ((unused)) int code, __attribute__ ((unused)) Datum arg){
  worker_should_restart = false;
  pg_atomic_write_u32(&worker_state->should_wake, 1); // ensure the remaining work will continue since we'll restart

  // ensure unlock happens in case of error
  unlock_extension();

  DisownLatch(&worker_state->latch);

  ev_monitor_close(worker_state);

  curl_multi_cleanup(worker_state->curl_mhandle);
  curl_global_cleanup();
}

// wait according to the wait type while ensuring interrupts are processed while waiting
static void wait_while_processing_interrupts(WorkerWait ww, bool *should_restart){
  switch(ww){
    case WORKER_WAIT_NO_TIMEOUT:
      WaitLatch(&worker_state->latch,
                WL_LATCH_SET | WL_EXIT_ON_PM_DEATH,
                no_timeout,
                PG_WAIT_EXTENSION);
      ResetLatch(&worker_state->latch);
      break;
    case WORKER_WAIT_ONE_SECOND:
      WaitLatch(&worker_state->latch, WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH, 1000, PG_WAIT_EXTENSION);
      ResetLatch(&worker_state->latch);
      break;
  }

  CHECK_FOR_INTERRUPTS();

  if (got_sighup) {
    got_sighup = false;
    ProcessConfigFile(PGC_SIGHUP);
  }

  if (pg_atomic_exchange_u32(&worker_state->got_restart, 0)){
    *should_restart = true;
  }
}

void pg_net_worker(__attribute__ ((unused)) Datum main_arg) {
  on_proc_exit(net_on_exit, 0);

  OwnLatch(&worker_state->latch);

  BackgroundWorkerUnblockSignals();
  pqsignal(SIGTERM, handle_sigterm);
  pqsignal(SIGHUP, handle_sighup);
  pqsignal(SIGUSR1, procsignal_sigusr1_handler);

  BackgroundWorkerInitializeConnection(guc_database_name, guc_username, 0);
  pgstat_report_appname("pg_net " EXTVERSION); // set appname for pg_stat_activity

  elog(INFO, "pg_net worker started with a config of: pg_net.ttl=%s, pg_net.batch_size=%d, pg_net.username=%s, pg_net.database_name=%s", guc_ttl, guc_batch_size, guc_username, guc_database_name);

  int curl_ret = curl_global_init(CURL_GLOBAL_ALL);
  if(curl_ret != CURLE_OK)
    ereport(ERROR, errmsg("curl_global_init() returned %s\n", curl_easy_strerror(curl_ret)));

  worker_state->epfd = event_monitor();

  if (worker_state->epfd < 0) {
    ereport(ERROR, errmsg("Failed to create event monitor file descriptor"));
  }

  worker_state->curl_mhandle = curl_multi_init();
  if(!worker_state->curl_mhandle)
    ereport(ERROR, errmsg("curl_multi_init()"));

  set_curl_mhandle(worker_state);

  publish_state(WS_RUNNING);

  do {

    if(!is_extension_loaded()){
      elog(DEBUG1, "pg_net worker waiting for extension to load");
      wait_while_processing_interrupts(WORKER_WAIT_ONE_SECOND, &worker_should_restart);
      continue;
    }

    lock_extension(); // lock the extension immediately after it's loaded

    uint32 expected = 1;
    if (!pg_atomic_compare_exchange_u32(&worker_state->should_wake, &expected, 0)){
      unlock_extension();
      elog(DEBUG1, "pg_net worker waiting for wake");
      wait_while_processing_interrupts(WORKER_WAIT_NO_TIMEOUT, &worker_should_restart);
      continue;
    }

    uint64 requests_consumed = 0;
    uint64 expired_responses = 0;

    do {
      expired_responses = delete_expired_responses(guc_ttl, guc_batch_size);

      elog(DEBUG1, "Deleted "UINT64_FORMAT" expired rows", expired_responses);

      StartTransactionCommand();
      PushActiveSnapshot(GetTransactionSnapshot());

      requests_consumed = consume_request_queue(worker_state->curl_mhandle, guc_batch_size, CurlMemContext);

      elog(DEBUG1, "Consumed "UINT64_FORMAT" request rows", requests_consumed);

      if(requests_consumed > 0){
        int running_handles = 0;
        int maxevents = guc_batch_size + 1; // 1 extra for the timer
        event events[maxevents];

        do {
          int nfds = wait_event(worker_state->epfd, events, maxevents, curl_handle_event_timeout_ms);
          if (nfds < 0) {
            int save_errno = errno;
            if(save_errno == EINTR) { // can happen when the wait is interrupted, for example when running under GDB. Just continue in this case.
              elog(DEBUG1, "wait_event() got %s, continuing", strerror(save_errno));
              continue;
            }
            else {
              ereport(ERROR, errmsg("wait_event() failed: %s", strerror(save_errno)));
              break;
            }
          }

          for (int i = 0; i < nfds; i++) {
            if (is_timer(events[i])) {
              EREPORT_MULTI(
                curl_multi_socket_action(worker_state->curl_mhandle, CURL_SOCKET_TIMEOUT, 0, &running_handles)
              );
            } else {
              int curl_event = get_curl_event(events[i]);
              int sockfd = get_socket_fd(events[i]);

              EREPORT_MULTI(
                curl_multi_socket_action(
                  worker_state->curl_mhandle,
                  sockfd,
                  curl_event,
                  &running_handles)
              );
            }

            insert_curl_responses(worker_state, CurlMemContext);
          }

          elog(DEBUG1, "Pending curl running_handles: %d", running_handles);
        } while (running_handles > 0); // run while there are curl handles, some won't finish in a single iteration since they could be slow and waiting for a timeout
      }

      PopActiveSnapshot();
      CommitTransactionCommand();

      MemoryContextReset(CurlMemContext);

      // slow down queue processing to avoid using too much CPU
      wait_while_processing_interrupts(WORKER_WAIT_ONE_SECOND, &worker_should_restart);

    } while (!worker_should_restart && (requests_consumed > 0 || expired_responses > 0));

  } while (!worker_should_restart);

  unlock_extension();

  publish_state(WS_EXITED);

  // causing a failure on exit will make the postmaster process restart the bg worker
  proc_exit(EXIT_FAILURE);
}

static void net_shmem_startup(void) {
  if (prev_shmem_startup_hook)
    prev_shmem_startup_hook();

  bool found;

  worker_state = ShmemInitStruct("pg_net worker state", sizeof(WorkerState), &found);

  if (!found) {
    pg_atomic_init_u32(&worker_state->got_restart, 0);
    pg_atomic_init_u32(&worker_state->status, WS_NOT_YET);
    pg_atomic_init_u32(&worker_state->should_wake, 1);
    InitSharedLatch(&worker_state->latch);

    ConditionVariableInit(&worker_state->cv);
    worker_state->epfd = 0;
    worker_state->curl_mhandle = NULL;
  }
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
    .bgw_restart_time = net_worker_restart_time_sec,
  });

  prev_shmem_startup_hook = shmem_startup_hook;
  shmem_startup_hook = net_shmem_startup;

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
                 PGC_SIGHUP, 0,
                 NULL, NULL, NULL);

  DefineCustomIntVariable("pg_net.batch_size",
                 "number of requests executed in one iteration of the background worker",
                 NULL,
                 &guc_batch_size,
                 200,
                 0, PG_INT16_MAX,
                 PGC_SIGHUP, 0,
                 NULL, NULL, NULL);

  DefineCustomStringVariable("pg_net.database_name",
                "Database where the worker will connect to",
                NULL,
                &guc_database_name,
                "postgres",
                PGC_SU_BACKEND, 0,
                NULL, NULL, NULL);

  DefineCustomStringVariable("pg_net.username",
                "Connection user for the worker",
                NULL,
                &guc_username,
                NULL,
                PGC_SU_BACKEND, 0,
                NULL, NULL, NULL);
}
