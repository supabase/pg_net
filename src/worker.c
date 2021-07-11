#include "postgres.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "fmgr.h"

#include <curl/multi.h>

static const char *urls[] = {
	"https://supabase.io/",
	"https://news.ycombinator.com/",
	"https://www.wikipedia.org",
	"https://aws.amazon.com/"
};

PG_MODULE_MAGIC;

void _PG_init(void);
void worker_main(Datum main_arg) pg_attribute_noreturn();

static volatile sig_atomic_t got_sigterm = false;

/* dummy callback */
static size_t cb(char *d, size_t n, size_t l, void *p)
{
	(void)d;
	(void)p;
	return n*l;
}

static void
handle_sigterm(SIGNAL_ARGS)
{
	int save_errno = errno;
	got_sigterm = true;
	if (MyProc)
		SetLatch(&MyProc->procLatch);
	errno = save_errno;
}

static int init(CURLM *cm, int i)
{
	CURL *eh = curl_easy_init();
	curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(eh, CURLOPT_HEADER, 0L);
	curl_easy_setopt(eh, CURLOPT_URL, urls[i]);
	curl_easy_setopt(eh, CURLOPT_PRIVATE, urls[i]);
	curl_easy_setopt(eh, CURLOPT_VERBOSE, 0L);
	return curl_multi_add_handle(cm, eh);
}

void
worker_main(Datum main_arg)
{
	CURLM *cm=NULL;
	CURL *eh=NULL;
	CURLMsg *msg=NULL;
	CURLcode return_code=0;
	int still_running=0, i=0, msgs_left=0;
	int http_status_code;
	int res;
	const char *szUrl;

	pqsignal(SIGTERM, handle_sigterm);

	BackgroundWorkerUnblockSignals();

	while (!got_sigterm)
	{
		/* Wait 10 seconds */
		WaitLatch(&MyProc->procLatch,
					WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
					10000L,
					PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);

		res = curl_global_init(CURL_GLOBAL_ALL);

		cm = curl_multi_init();

		if(res) {
			elog(ERROR, "error: curl_global_init() returned %d\n", res);
		}

		for (i = 0; i < 4; ++i) {
			res = init(cm, i);
			if(res) {
				elog(ERROR, "error: init() returned %d\n", res);
			}
		}

		res = curl_multi_perform(cm, &still_running);
		if(res != CURLM_OK) {
				elog(ERROR, "error: curl_multi_perform() returned %d\n", res);
		}

		do {
				int numfds=0;
				/* Wait max. 30 seconds */
				res = curl_multi_wait(cm, NULL, 0, 30*1000, &numfds);
				if(res != CURLM_OK) {
						elog(ERROR, "error: curl_multi_wait() returned %d\n", res);
				}

				curl_multi_perform(cm, &still_running);

		} while(still_running);

		while ((msg = curl_multi_info_read(cm, &msgs_left))) {
				if (msg->msg == CURLMSG_DONE) {
						eh = msg->easy_handle;

						return_code = msg->data.result;
						if(return_code!=CURLE_OK) {
								elog(ERROR, "CURL error code: %d\n", msg->data.result);
								continue;
						}

						// Get HTTP status code
						http_status_code=0;
						szUrl = NULL;

						curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
						curl_easy_getinfo(eh, CURLINFO_PRIVATE, &szUrl);

						elog(INFO, "GET of %s returned http status code %d\n", szUrl, http_status_code);

						curl_multi_remove_handle(cm, eh);
						curl_easy_cleanup(eh);
				}
				else {
						elog(ERROR, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
				}
		}

		curl_multi_cleanup(cm);
	}

	proc_exit(0);
}

void
_PG_init(void)
{
	BackgroundWorker worker;

	MemSet(&worker, 0, sizeof(BackgroundWorker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "curl_worker");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "worker_main");
	snprintf(worker.bgw_name, BGW_MAXLEN, "curl worker");
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);
}
