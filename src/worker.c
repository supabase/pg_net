// clang-format off
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

#include "access/hash.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

#include "utils/jsonb.h"

#include <curl/multi.h>

PG_MODULE_MAGIC;

void _PG_init(void);
void worker_main(Datum main_arg) pg_attribute_noreturn();
bool isExtensionLoaded(void);

typedef struct _CurlData
{
	int64 id;
	StringInfo body;
	JsonbParseState *headers;
} CurlData;

static volatile sig_atomic_t got_sigterm = false;

static void
handle_sigterm(SIGNAL_ARGS)
{
	int save_errno = errno;
	got_sigterm = true;
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
	if(!firstLine && !lastLine){
	/*TODO: make the parsing more robust, test with invalid headers*/
		char *token;
		char *tmp = pstrdup(contents);
		JsonbValue	key, val;

		elog(DEBUG1, "FullHeaderLine: %s\n", tmp);

		/*The header comes as "Header-Key: val", split by whitespace and ditch the colon later*/
		token = strtok(tmp, " ");
		elog(DEBUG1, "Key: %s\n", token);

		key.type = jbvString;
		key.val.string.val = token;
		/*strlen - 1 because we ditch the last char - the colon*/
		key.val.string.len = strlen(token) - 1;
		(void)pushJsonbValue(&headers, WJB_KEY, &key);

		/*Every header line ends with CRLF, split and remove it*/
		token = strtok(NULL, "\r\n");
		elog(DEBUG1, "Value: %s\n", token);

		val.type = jbvString;
		val.val.string.val = token;
		val.val.string.len = strlen(token);
		(void)pushJsonbValue(&headers, WJB_VALUE, &val);
	}

	return realsize;
}

static int init(CURLM *cm, char *url, int64 id, HTAB *curlDataMap)
{
	CURL *eh = curl_easy_init();

	CurlData *cdata = NULL;
	StringInfo body = makeStringInfo();
	JsonbParseState *headers = NULL;
	bool isPresent = false;

	cdata = hash_search(curlDataMap, &id, HASH_ENTER, &isPresent);
	if (!isPresent)
	{
		cdata->id = id;
		cdata->body = body;
		(void)pushJsonbValue(&headers, WJB_BEGIN_OBJECT, NULL);
		cdata->headers = headers;
	}

	curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, body_cb);
	curl_easy_setopt(eh, CURLOPT_WRITEDATA, cdata->body);
	curl_easy_setopt(eh, CURLOPT_HEADERFUNCTION, header_cb);
	curl_easy_setopt(eh, CURLOPT_HEADERDATA, cdata->headers);
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

	extensionOid = get_extension_oid("pg_net", true);

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
	int still_running=0, msgs_left=0;
	int http_status_code;
	int res;

	HTAB *curlDataMap = NULL;
	HASHCTL info;
	int hashFlags = 0;

	pqsignal(SIGTERM, handle_sigterm);

	BackgroundWorkerUnblockSignals();

	BackgroundWorkerInitializeConnection("postgres", NULL, 0);

	MemSet(&info, 0, sizeof(info));
	info.keysize = sizeof(int64);
	info.entrysize = sizeof(CurlData);
	info.hash = tag_hash;
	info.hcxt = AllocSetContextCreate(CurrentMemoryContext,
											"pg_net context",
											ALLOCSET_DEFAULT_MINSIZE,
											ALLOCSET_DEFAULT_INITSIZE,
											ALLOCSET_DEFAULT_MAXSIZE);
	hashFlags = (HASH_ELEM | HASH_FUNCTION | HASH_CONTEXT);

	curlDataMap = hash_create("pg_net curl data", 1024, &info, hashFlags);

	while (!got_sigterm)
	{
		StringInfoData	select_query;
		StringInfoData	query_insert_response_ok;
		StringInfoData	query_insert_response_bad;

		/* Wait 10 seconds */
		WaitLatch(&MyProc->procLatch,
					WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
					1000L,
					PG_WAIT_EXTENSION);
		ResetLatch(&MyProc->procLatch);

		if(!isExtensionLoaded()){
			elog(DEBUG1, "Extension not loaded");
			continue;
		}

		StartTransactionCommand();
		PushActiveSnapshot(GetTransactionSnapshot());
		SPI_connect();

		initStringInfo(&select_query);

		appendStringInfo(&select_query, "\
			SELECT\
			  q.id, q.url\
			FROM net.http_request_queue q \
			LEFT JOIN net.http_response r ON q.id = r.id \
			WHERE r.id IS NULL");

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

					elog(DEBUG1, "Making a request to %s with id %ld", url, id);

					res = init(cm, url, id, curlDataMap);

					if(res) {
						elog(ERROR, "error: init() returned %d\n", res);
					}
					res = curl_multi_perform(cm, &still_running);
					if(res != CURLM_OK) {
							elog(ERROR, "error: curl_multi_perform() returned %d\n", res);
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

		initStringInfo(&query_insert_response_ok);
		appendStringInfo(&query_insert_response_ok, "\
			insert into net.http_response(id, status_code, body, headers, content_type, timed_out) values ($1, $2, $3, $4, $5, $6)");

		initStringInfo(&query_insert_response_bad);
		appendStringInfo(&query_insert_response_bad, "\
			insert into net.http_response(id, error_msg) values ($1, $2)");

		while ((msg = curl_multi_info_read(cm, &msgs_left))) {
				int64 id;
				CURLcode return_code=0;
				bool isPresent = false;

				if (msg->msg == CURLMSG_DONE) {
						eh = msg->easy_handle;

						return_code = msg->data.result;
						curl_easy_getinfo(eh, CURLINFO_PRIVATE, &id);

						if (return_code != CURLE_OK) {
							int argCount = 2;
							Oid argTypes[2];
							Datum argValues[2];
							const char *error_msg = curl_easy_strerror(return_code);

							argTypes[0] = INT8OID;
							argValues[0] = Int64GetDatum(id);

							argTypes[1] = CSTRINGOID;
							argValues[1] = CStringGetDatum(error_msg);

							elog(DEBUG1, "%s\n", error_msg);

							if (SPI_execute_with_args(query_insert_response_bad.data, argCount, argTypes, argValues, NULL,
											false, 1) != SPI_OK_INSERT)
							{
								elog(ERROR, "SPI_exec failed: %s", query_insert_response_bad.data);
							}
						} else {
							int argCount = 6;
							Oid argTypes[6];
							Datum argValues[6];
							char nulls[6];
							CurlData *cdata = NULL;
							char *contentType = NULL;
							bool timedOut = false;

							curl_easy_getinfo(eh, CURLINFO_RESPONSE_CODE, &http_status_code);
							curl_easy_getinfo(eh, CURLINFO_CONTENT_TYPE, &contentType);
							curl_easy_getinfo(eh, CURLINFO_PRIVATE, &id);

							elog(DEBUG1, "GET of %ld returned http status code %d\n", id, http_status_code);

							cdata = hash_search(curlDataMap, &id, HASH_FIND, &isPresent);

							argTypes[0] = INT8OID;
							argValues[0] = Int64GetDatum(id);
							nulls[0] = ' ';

							argTypes[1] = INT4OID;
							argValues[1] = Int32GetDatum(http_status_code);
							nulls[1] = ' ';

							argTypes[2] = CSTRINGOID;
							argValues[2] = CStringGetDatum(cdata->body->data);
							if(cdata->body->data[0] == '\0')
								nulls[2] = 'n';
							else
								nulls[2] = ' ';

							argTypes[3] = JSONBOID;
							argValues[3] = JsonbPGetDatum(JsonbValueToJsonb(pushJsonbValue(&cdata->headers, WJB_END_OBJECT, NULL)));
							nulls[3] = ' ';

							argTypes[4] = CSTRINGOID;
							argValues[4] = CStringGetDatum(contentType);
							if(!contentType)
								nulls[4] = 'n';
							else
								nulls[4] = ' ';

							argTypes[5] = BOOLOID;
							argValues[5] = BoolGetDatum(timedOut);
							nulls[5] = ' ';

							if (SPI_execute_with_args(query_insert_response_ok.data, argCount, argTypes, argValues, nulls,
											false, 1) != SPI_OK_INSERT)
							{
								elog(ERROR, "SPI_exec failed: %s", query_insert_response_ok.data);
							}

							pfree(cdata->body->data);
							pfree(cdata->body);
						}

						curl_multi_remove_handle(cm, eh);
						curl_easy_cleanup(eh);
						hash_search(curlDataMap, &id, HASH_REMOVE, &isPresent);
				} else {
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
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pg_net");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "worker_main");
	snprintf(worker.bgw_name, BGW_MAXLEN, "pg_net worker");
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;
	RegisterBackgroundWorker(&worker);
}
// clang-format on
