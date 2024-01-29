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

#include "miscadmin.h"

#include "utils/builtins.h"

#include "access/hash.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

#include "utils/jsonb.h"
#include <utils/guc.h>

#include "tcop/utility.h"

#include <curl/curl.h>
#include <curl/multi.h>

#include "util.h"

PG_MODULE_MAGIC;

static char *ttl = "6 hours";
static int batch_size = 500;
static char* database_name = "postgres";

void _PG_init(void);
PGDLLEXPORT void worker_main(Datum main_arg) pg_attribute_noreturn();
bool is_extension_loaded(void);

typedef struct _CurlData
{
	int64 id;
	StringInfo body;
	JsonbParseState* response_headers;
	struct curl_slist* request_headers;
} CurlData;

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
	int			save_errno = errno;
	got_sighup = true;
	if (MyProc)
		SetLatch(&MyProc->procLatch);
	errno = save_errno;
}

static size_t
body_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	StringInfo si = (StringInfo)userp;
	appendBinaryStringInfo(si, (const char*)contents, (int)realsize);
	return realsize;
}

static size_t
header_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	JsonbParseState *headers = (JsonbParseState *)userp;

	/* per curl docs, the status code is included in the header data
	 * (it starts with: HTTP/1.1 200 OK or HTTP/2 200 OK)*/
	bool firstLine = strncmp(contents, "HTTP/", 5) == 0;
	/* the final(end of headers) last line is empty - just a CRLF */
	bool lastLine = strncmp(contents, "\r\n", 2) == 0;

	/*Ignore non-header data in the first header line and last header line*/
	if (!firstLine && !lastLine) {
			parseHeaders(contents, headers);
	}

	return realsize;
}

static CURLMcode init(CURLM *cm, char *method, char *url, int timeout_milliseconds, struct curl_slist *request_headers, char *reqBody, int64 id, CurlData *cdata)
{
	CURL *eh = curl_easy_init();

	StringInfo body = makeStringInfo();
	JsonbParseState *response_headers = NULL;

	cdata->body = body;
	(void)pushJsonbValue(&response_headers, WJB_BEGIN_OBJECT, NULL);
	cdata->response_headers = response_headers;
	cdata->id = id;
	cdata->request_headers = request_headers;

	request_headers = curl_slist_append(request_headers, "User-Agent: pg_net/" EXTVERSION);

	if (strcasecmp(method, "GET") == 0) {
		if (reqBody) {
			curl_easy_setopt(eh, CURLOPT_POSTFIELDS, reqBody);
			curl_easy_setopt(eh, CURLOPT_CUSTOMREQUEST, "GET");
		}
	}

	if (strcasecmp(method, "POST") == 0) {
		if (reqBody) {
			curl_easy_setopt(eh, CURLOPT_POSTFIELDS, reqBody);
		}
		else {
			curl_easy_setopt(eh, CURLOPT_POST, 1);
			curl_easy_setopt(eh, CURLOPT_POSTFIELDSIZE, 0);
		}
	}

	if (strcasecmp(method, "DELETE") == 0) {
		curl_easy_setopt(eh, CURLOPT_CUSTOMREQUEST, "DELETE");
	}

	curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, body_cb);
	curl_easy_setopt(eh, CURLOPT_WRITEDATA, cdata->body);
	curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, header_cb);
	curl_easy_setopt(eh, CURLOPT_HEADERDATA, cdata->response_headers);
	curl_easy_setopt(eh, CURLOPT_HEADER, 0L);
	curl_easy_setopt(eh, CURLOPT_URL, url);
	curl_easy_setopt(eh, CURLOPT_HTTPHEADER, cdata->request_headers);
	curl_easy_setopt(eh, CURLOPT_TIMEOUT_MS, timeout_milliseconds);
	curl_easy_setopt(eh, CURLOPT_PRIVATE, cdata);
	curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, true);
	if (log_min_messages <= DEBUG1)
		curl_easy_setopt(eh, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(eh, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
	return curl_multi_add_handle(cm, eh);
}

bool is_extension_loaded(){
	Oid extensionOid;

	StartTransactionCommand();
	extensionOid = get_extension_oid("pg_net", true);
	CommitTransactionCommand();

	return OidIsValid(extensionOid);
}

void
worker_main(Datum main_arg)
{
	CURLM *cm=NULL;
	CURL *eh=NULL;
	CURLMsg *msg=NULL;
	int still_running=0, msgs_left=0;
	int res;

	pqsignal(SIGTERM, handle_sigterm);
	pqsignal(SIGHUP, handle_sighup);
	pqsignal(SIGUSR1, procsignal_sigusr1_handler);

	BackgroundWorkerUnblockSignals();

	BackgroundWorkerInitializeConnection(database_name, NULL, 0);

	elog(INFO, "pg_net worker started with a config of: pg_net.ttl=%s, pg_net.batch_size=%d, pg_net.database_name=%s", ttl, batch_size, database_name);

	while (!got_sigterm)
	{
		WaitLatch(&MyProc->procLatch,
					WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
					1000L,
					PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);

		CHECK_FOR_INTERRUPTS();

		if(!is_extension_loaded()){
      elog(DEBUG2, "pg_net worker: extension not yet loaded");
			continue;
		}

		if (got_sighup) {
			got_sighup = false;
			ProcessConfigFile(PGC_SIGHUP);
		}

		SetCurrentStatementStartTimestamp();
		StartTransactionCommand();
		PushActiveSnapshot(GetTransactionSnapshot());
		SPI_connect();

		{
			int ttl_query_rc = SPI_execute_with_args("\
				WITH\
				rows AS (\
					SELECT ctid\
					FROM net._http_response\
					WHERE created < now() - $1\
					ORDER BY created\
					LIMIT $2\
				)\
				DELETE FROM net._http_response r\
				USING rows WHERE r.ctid = rows.ctid",
				2,
				(Oid[]){INTERVALOID, INT4OID},
				(Datum[]){
				  DirectFunctionCall3(interval_in, CStringGetDatum(ttl), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1))
				, Int32GetDatum(batch_size)
				}, NULL, false, 0);

			if (ttl_query_rc != SPI_OK_DELETE)
			{
        ereport(ERROR, errmsg("Error expiring response table rows: %s", SPI_result_code_string(ttl_query_rc)));
			}
		}

		{
			int queue_query_rc = SPI_execute_with_args("\
				WITH\
				rows AS (\
					SELECT id\
					FROM net.http_request_queue\
					ORDER BY id\
					LIMIT $1\
				)\
				DELETE FROM net.http_request_queue q\
				USING rows WHERE q.id = rows.id\
				RETURNING q.id, q.method, q.url, timeout_milliseconds, array(select key || ': ' || value from jsonb_each_text(q.headers)), q.body",
				1,
				(Oid[]){INT4OID},
				(Datum[]){Int32GetDatum(batch_size)},
				NULL, false, 0);

			if (queue_query_rc == SPI_OK_DELETE_RETURNING)
			{
				bool tupIsNull = false;

				res = curl_global_init(CURL_GLOBAL_ALL);

				if(res) {
					elog(ERROR, "error: curl_global_init() returned %d\n", res);
				}

				cm = curl_multi_init();

				for (int j = 0; j < SPI_processed; j++)
				{
						struct curl_slist *request_headers = NULL;

						int64 id = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 1, &tupIsNull));
						char *method = TextDatumGetCString(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 2, &tupIsNull));
						char *url = TextDatumGetCString(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 3, &tupIsNull));
						int32 timeout_milliseconds = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 4, &tupIsNull));

						Datum headersBin;
						Datum bodyBin;
						ArrayType *pgHeaders;
						char *body = NULL;
						CurlData *cdata;

						if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "POST") != 0 && strcasecmp(method, "DELETE") != 0) {
							elog(ERROR, "error: Unsupported request method %s\n", method);
						}

						headersBin = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 5, &tupIsNull);
						if (!tupIsNull) {
							pgHeaders = DatumGetArrayTypeP(headersBin);
							request_headers = pg_text_array_to_slist(pgHeaders, request_headers);
						}
						bodyBin = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 6, &tupIsNull);
						if (!tupIsNull) body = TextDatumGetCString(bodyBin);

						cdata = palloc(sizeof(CurlData));

						res = init(cm, method, url, timeout_milliseconds, request_headers, body, id, cdata);

						if(res) {
							elog(ERROR, "error: init() returned %d\n", res);
						}
				}
			}
			else
			{
        ereport(ERROR, errmsg("Error getting http request queue: %s", SPI_result_code_string(queue_query_rc)));
			}
		}

		do {
				int numfds=0;

				res = curl_multi_perform(cm, &still_running);

				if(res != CURLM_OK) {
						elog(ERROR, "error: curl_multi_perform() returned %d\n", res);
				}

				/*wait at least 1 second(1000 ms) in case all responses are slow*/
				/*this avoids busy waiting and higher CPU usage*/
				res = curl_multi_wait(cm, NULL, 0, 1000, &numfds);

				if(res != CURLM_OK) {
						elog(ERROR, "error: curl_multi_wait() returned %d\n", res);
				}
		} while(still_running);

		while ((msg = curl_multi_info_read(cm, &msgs_left))) {
				if (msg->msg == CURLMSG_DONE) {
						CURLcode return_code = msg->data.result;
						eh = msg->easy_handle;

						if (return_code != CURLE_OK) {
							CurlData *cdata = NULL;

							curl_easy_getinfo(eh, CURLINFO_PRIVATE, &cdata);

							int failed_query_rc = SPI_execute_with_args("\
									insert into net._http_response(id, error_msg) values ($1, $2)",
									2,
									(Oid[]){INT8OID, CSTRINGOID},
									(Datum[]){Int64GetDatum(cdata->id), CStringGetDatum(curl_easy_strerror(return_code))},
									NULL, false, 1);

							if (failed_query_rc != SPI_OK_INSERT)
							{
								ereport(ERROR, errmsg("Error when inserting failed response: %s", SPI_result_code_string(failed_query_rc)));
							}
						} else {
							CurlData *cdata = NULL;
							char *contentType = NULL;
							int http_status_code;

							curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
							curl_easy_getinfo(eh, CURLINFO_CONTENT_TYPE, &contentType);
							curl_easy_getinfo(eh, CURLINFO_PRIVATE, &cdata);

							int succ_query_rc = SPI_execute_with_args("\
									insert into net._http_response(id, status_code, content, headers, content_type, timed_out) values ($1, $2, $3, $4, $5, $6)",
									6,
									(Oid[]){INT8OID, INT4OID, CSTRINGOID, JSONBOID, CSTRINGOID, BOOLOID},
									(Datum[]){
										Int64GetDatum(cdata->id)
									, Int32GetDatum(http_status_code)
									, CStringGetDatum(cdata->body->data)
									, JsonbPGetDatum(JsonbValueToJsonb(pushJsonbValue(&cdata->response_headers, WJB_END_OBJECT, NULL)))
									, CStringGetDatum(contentType)
									// TODO Why is this hardcoded?
									, BoolGetDatum(false)
									},
									(char[6]){
										' '
									, [2] = cdata->body->data[0] == '\0'? 'n' : ' '
									, [4] = !contentType? 'n' :' '
									},
									false, 1);

							if ( succ_query_rc != SPI_OK_INSERT)
							{
								ereport(ERROR, errmsg("Error when inserting successful response: %s", SPI_result_code_string(succ_query_rc)));
							}

							pfree(cdata->body->data);
							pfree(cdata->body);
							curl_slist_free_all(cdata->request_headers);
							pfree(cdata);
						}

						curl_multi_remove_handle(cm, eh);
						curl_easy_cleanup(eh);
				} else {
						elog(ERROR, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
				}
		}

		curl_multi_cleanup(cm);

		SPI_finish();
		PopActiveSnapshot();
		CommitTransactionCommand();
	}

	// causing a failure on exit will make the postmaster process restart the bg worker
	proc_exit(EXIT_FAILURE);
}

void
_PG_init(void)
{
	BackgroundWorker worker;

	if (IsBinaryUpgrade) {
			return;
	}

	if (!process_shared_preload_libraries_in_progress) {
			ereport(ERROR, errmsg("pg_net is not in shared_preload_libraries"),
							errhint("Add pg_net to the shared_preload_libraries "
											"configuration variable in postgresql.conf."));
	}

	MemSet(&worker, 0, sizeof(BackgroundWorker));
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_net");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "worker_main");
	snprintf(worker.bgw_name, BGW_MAXLEN, "pg_net " EXTVERSION " worker");
	worker.bgw_restart_time = 1;
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);

	DefineCustomStringVariable("pg_net.ttl",
							   "time to live for request/response rows",
							   "should be a valid interval type",
							   &ttl,
							   "6 hours",
							   PGC_SIGHUP, 0,
								 NULL, NULL, NULL);

	DefineCustomIntVariable("pg_net.batch_size",
							   "number of requests executed in one iteration of the background worker",
							   NULL,
							   &batch_size,
							   200,
								 0, PG_INT16_MAX,
							   PGC_SIGHUP, 0,
								 NULL, NULL, NULL);

	DefineCustomStringVariable("pg_net.database_name",
								"Database where pg_net tables are located",
								NULL,
								&database_name,
								"postgres",
								PGC_SIGHUP, 0,
								NULL, NULL, NULL);
}
