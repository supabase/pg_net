#include "postgres.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "fmgr.h"

#include "access/xact.h"
#include "executor/spi.h"
#include "utils/snapmgr.h"

#include "commands/extension.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_type.h"

#include "utils/builtins.h"

#include <curl/multi.h>

PG_MODULE_MAGIC;

void _PG_init(void);
void worker_main(Datum main_arg) pg_attribute_noreturn();
bool isExtensionLoaded(void);

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

static int init(CURLM *cm, char *url, int64 id)
{
	CURL *eh = curl_easy_init();
	curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(eh, CURLOPT_HEADER, 0L);
	curl_easy_setopt(eh, CURLOPT_URL, url);
	curl_easy_setopt(eh, CURLOPT_PRIVATE, id);
	curl_easy_setopt(eh, CURLOPT_VERBOSE, 0L);
	return curl_multi_add_handle(cm, eh);
}

bool isExtensionLoaded(){
	Oid extensionOid;

	StartTransactionCommand();
	PushActiveSnapshot(GetTransactionSnapshot());

	extensionOid = get_extension_oid("curl_worker", true);

	PopActiveSnapshot();
	CommitTransactionCommand();

	return extensionOid != InvalidOid;
}

void
worker_main(Datum main_arg)
{
	CURLM *cm=NULL;
	CURL *eh=NULL;
	CURLMsg *msg=NULL;
	CURLcode return_code=0;
	int still_running=0, msgs_left=0;
	int http_status_code;
	int res;

	pqsignal(SIGTERM, handle_sigterm);

	BackgroundWorkerUnblockSignals();

	BackgroundWorkerInitializeConnection("postgres", NULL, 0);

	while (!got_sigterm)
	{
		StringInfoData	select_query;
		StringInfoData	insert_query;
		StringInfoData	update_query;

		/* Wait 10 seconds */
		WaitLatch(&MyProc->procLatch,
					WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
					10000L,
					PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);

		if(!isExtensionLoaded()){
			elog(INFO, "Extension not loaded");
			continue;
		}

		StartTransactionCommand();
		PushActiveSnapshot(GetTransactionSnapshot());
		SPI_connect();

		initStringInfo(&select_query);

		appendStringInfo(&select_query, "SELECT id, url, is_completed FROM http.request_queue");

		if (SPI_execute(select_query.data, true, 0) == SPI_OK_SELECT)
		{
			bool tupIsNull = false;

			res = curl_global_init(CURL_GLOBAL_ALL);

			if(res) {
				elog(ERROR, "error: curl_global_init() returned %d\n", res);
			}

			cm = curl_multi_init();

			for (int j = 0; j < SPI_processed; j++)
			{
					int64 id = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 1, &tupIsNull));
					char *url = TextDatumGetCString(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 2, &tupIsNull));
					bool is_completed = DatumGetBool(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 3, &tupIsNull));
					elog(INFO, "ID: %ld", id);
					elog(INFO, "URL: %s", url);
					elog(INFO, "Completed: %d", is_completed);

					if(!is_completed){
						res = init(cm, url, id);
						if(res) {
							elog(ERROR, "error: init() returned %d\n", res);
						}
						res = curl_multi_perform(cm, &still_running);
						if(res != CURLM_OK) {
								elog(ERROR, "error: curl_multi_perform() returned %d\n", res);
						}
					}
			}
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

		initStringInfo(&insert_query);
		appendStringInfo(&insert_query, "insert into http.response(id, http_status_code) values ($1, $2)");

		initStringInfo(&update_query);
		appendStringInfo(&update_query, "update http.request_queue set is_completed = true where id = $1");

		while ((msg = curl_multi_info_read(cm, &msgs_left))) {
				int64 id;
				Oid argTypes[2];
				Datum argValues[2];

				if (msg->msg == CURLMSG_DONE) {
						eh = msg->easy_handle;

						return_code = msg->data.result;
						if(return_code!=CURLE_OK) {
								elog(ERROR, "CURL error code: %d\n", msg->data.result);
								continue;
						}

						curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
						curl_easy_getinfo(eh, CURLINFO_PRIVATE, &id);

						elog(INFO, "GET of %ld returned http status code %d\n", id, http_status_code);

						argTypes[0] = INT8OID;
						argValues[0] = Int64GetDatum(id);

						argTypes[1] = INT4OID;
						argValues[1] = Int32GetDatum(http_status_code);

						if (SPI_execute_with_args(insert_query.data, 2, argTypes, argValues, NULL,
										false, 1) != SPI_OK_INSERT)
						{
							elog(ERROR, "SPI_exec failed: %s", insert_query.data);
						}

						if (SPI_execute_with_args(update_query.data, 1, argTypes, argValues, NULL,
										false, 1) != SPI_OK_UPDATE)
						{
							elog(ERROR, "SPI_exec failed: %s", update_query.data);
						}

						curl_multi_remove_handle(cm, eh);
						curl_easy_cleanup(eh);
				}
				else {
						elog(ERROR, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
				}
		}

		curl_multi_cleanup(cm);

		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();
	}

	proc_exit(0);
}

void
_PG_init(void)
{
	BackgroundWorker worker;

	MemSet(&worker, 0, sizeof(BackgroundWorker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "curl_worker");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "worker_main");
	snprintf(worker.bgw_name, BGW_MAXLEN, "curl worker");
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);
}
