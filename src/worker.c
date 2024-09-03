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

#include "util.h"

#define MIN_LIBCURL_VERSION_NUM 0x075300 // This is the 7.83.0 version in hex as defined in curl/curlver.h
_Static_assert(LIBCURL_VERSION_NUM, "libcurl >= 7.83.0 is required"); // test for older libcurl versions that don't even have LIBCURL_VERSION_NUM defined (e.g. libcurl 6.5).
_Static_assert(LIBCURL_VERSION_NUM >= MIN_LIBCURL_VERSION_NUM, "libcurl >= 7.83.0 is required");

PG_MODULE_MAGIC;

static char *guc_ttl = "6 hours";
static int guc_batch_size = 500;
static char* guc_database_name = "postgres";

void _PG_init(void);
PGDLLEXPORT void pg_net_worker(Datum main_arg) pg_attribute_noreturn();

typedef struct {
  int64 id;
  StringInfo body;
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
  int     save_errno = errno;
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

static bool is_extension_loaded(){
  Oid extensionOid;

  StartTransactionCommand();
  extensionOid = get_extension_oid("pg_net", true);
  CommitTransactionCommand();

  return OidIsValid(extensionOid);
}

static void delete_expired_responses(char *ttl){
  int ret_code = SPI_execute_with_args("\
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
    , Int32GetDatum(guc_batch_size)
    }, NULL, false, 0);

  if (ret_code != SPI_OK_DELETE)
  {
    ereport(ERROR, errmsg("Error expiring response table rows: %s", SPI_result_code_string(ret_code)));
  }
}

static void insert_failure_response(CURLcode return_code, int64 id){
  int ret_code = SPI_execute_with_args("\
      insert into net._http_response(id, error_msg) values ($1, $2)",
      2,
      (Oid[]){INT8OID, CSTRINGOID},
      (Datum[]){Int64GetDatum(id), CStringGetDatum(curl_easy_strerror(return_code))},
      NULL, false, 1);

  if (ret_code != SPI_OK_INSERT)
  {
    ereport(ERROR, errmsg("Error when inserting failed response: %s", SPI_result_code_string(ret_code)));
  }
}

static void insert_success_response(CurlData *cdata, long http_status_code, char *contentType, Jsonb *jsonb_headers){
  int ret_code = SPI_execute_with_args("\
      insert into net._http_response(id, status_code, content, headers, content_type, timed_out) values ($1, $2, $3, $4, $5, $6)",
      6,
      (Oid[]){INT8OID, INT4OID, CSTRINGOID, JSONBOID, CSTRINGOID, BOOLOID},
      (Datum[]){
        Int64GetDatum(cdata->id)
      , Int32GetDatum(http_status_code)
      , CStringGetDatum(cdata->body->data)
      , JsonbPGetDatum(jsonb_headers)
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

  if ( ret_code != SPI_OK_INSERT)
  {
    ereport(ERROR, errmsg("Error when inserting successful response: %s", SPI_result_code_string(ret_code)));
  }
}

static void pfree_curl_data(CurlData *cdata){
  pfree(cdata->body->data);
  pfree(cdata->body);
  if(cdata->request_headers) //curl_slist_free_all already handles the NULL case, but be explicit about it
    curl_slist_free_all(cdata->request_headers);
  pfree(cdata);
}

static void init_curl_handle(CURLM *curl_mhandle, CurlData *cdata, char *url, char *reqBody, char *method, int32 timeout_milliseconds){
  CURL *curl_ez_handle = curl_easy_init();
  if(!curl_ez_handle)
    ereport(ERROR, errmsg("curl_easy_init()"));

  if (strcasecmp(method, "GET") == 0) {
    if (reqBody) {
      CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_POSTFIELDS, reqBody);
      CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_CUSTOMREQUEST, "GET");
    }
  }

  if (strcasecmp(method, "POST") == 0) {
    if (reqBody) {
      CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_POSTFIELDS, reqBody);
    }
    else {
      CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_POST, 1);
      CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_POSTFIELDSIZE, 0);
    }
  }

  if (strcasecmp(method, "DELETE") == 0) {
    CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
  }

  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_WRITEFUNCTION, body_cb);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_WRITEDATA, cdata->body);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_HEADER, 0L);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_URL, url);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_HTTPHEADER, cdata->request_headers);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_TIMEOUT_MS, timeout_milliseconds);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_PRIVATE, cdata);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_FOLLOWLOCATION, true);
  if (log_min_messages <= DEBUG1)
    CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_VERBOSE, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500 /* libcurl 7.85.0 */
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_PROTOCOLS_STR, "http,https");
#else
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

  CURLMcode code = curl_multi_add_handle(curl_mhandle, curl_ez_handle);
  if(code != CURLM_OK)
    ereport(ERROR, errmsg("curl_multi_add_handle returned %s", curl_multi_strerror(code)));
}

static void consume_request_queue(CURLM *curl_mhandle){
  int ret_code = SPI_execute_with_args("\
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
    (Datum[]){Int32GetDatum(guc_batch_size)},
    NULL, false, 0);

  if (ret_code != SPI_OK_DELETE_RETURNING)
    ereport(ERROR, errmsg("Error getting http request queue: %s", SPI_result_code_string(ret_code)));

  for (int j = 0; j < SPI_processed; j++) {
    bool tupIsNull = false;

    int64 id = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 1, &tupIsNull));
    EREPORT_NULL_ATTR(tupIsNull, id);

    char *method = TextDatumGetCString(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 2, &tupIsNull));
    EREPORT_NULL_ATTR(tupIsNull, method);

    char *url = TextDatumGetCString(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 3, &tupIsNull));
    EREPORT_NULL_ATTR(tupIsNull, url);

    int32 timeout_milliseconds = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 4, &tupIsNull));
    EREPORT_NULL_ATTR(tupIsNull, timeout_milliseconds);

    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "POST") != 0 && strcasecmp(method, "DELETE") != 0) {
      ereport(ERROR, errmsg("Unsupported request method %s", method));
    }

    CurlData *cdata = palloc(sizeof(CurlData));

    Datum headersBin = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 5, &tupIsNull);

    if (!tupIsNull) {
      ArrayType *pgHeaders = DatumGetArrayTypeP(headersBin);
      struct curl_slist *request_headers = NULL;

      request_headers = pg_text_array_to_slist(pgHeaders, request_headers);

      CURL_SLIST_APPEND(request_headers, "User-Agent: pg_net/" EXTVERSION);

      cdata->request_headers = request_headers;
    }

    char *reqBody = NULL;
    Datum bodyBin = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 6, &tupIsNull);
    if (!tupIsNull) reqBody = TextDatumGetCString(bodyBin);

    cdata->body = makeStringInfo();
    cdata->id = id;

    init_curl_handle(curl_mhandle, cdata, url, reqBody, method, timeout_milliseconds);
  }
}

static Jsonb *jsonb_headers_from_curl_handle(CURL *ez_handle){
  struct curl_header *header, *prev = NULL;

  JsonbParseState *headers = NULL;
  (void)pushJsonbValue(&headers, WJB_BEGIN_OBJECT, NULL);

  while((header = curl_easy_nextheader(ez_handle, CURLH_HEADER, 0, prev))) {
    JsonbValue key   = {.type = jbvString, .val = {.string = {.val = header->name,  .len = strlen(header->name)}}};
    JsonbValue value = {.type = jbvString, .val = {.string = {.val = header->value, .len = strlen(header->value)}}};
    (void)pushJsonbValue(&headers, WJB_KEY,   &key);
    (void)pushJsonbValue(&headers, WJB_VALUE, &value);
    prev = header;
  }

  Jsonb *jsonb_headers = JsonbValueToJsonb(pushJsonbValue(&headers, WJB_END_OBJECT, NULL));

  return jsonb_headers;
}

static void insert_curl_responses(CURLM *curl_mhandle){
  int still_running = 0;

  do {
    int numfds = 0;

    int res = curl_multi_perform(curl_mhandle, &still_running);

    if(res != CURLM_OK) {
      ereport(ERROR, errmsg("error: curl_multi_perform() returned %d", res));
    }

    /*wait at least 1 second(1000 ms) in case all responses are slow*/
    /*this avoids busy waiting and higher CPU usage*/
    res = curl_multi_wait(curl_mhandle, NULL, 0, 1000, &numfds);

    if(res != CURLM_OK) {
      ereport(ERROR, errmsg("error: curl_multi_wait() returned %d", res));
    }
  } while(still_running);

  int msgs_left=0;
  CURLMsg *msg = NULL;

  while ((msg = curl_multi_info_read(curl_mhandle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURLcode return_code = msg->data.result;
      CURL *ez_handle= msg->easy_handle;
      CurlData *cdata = NULL;
      CURL_EZ_GETINFO(ez_handle, CURLINFO_PRIVATE, &cdata);

      if (return_code != CURLE_OK) {
        insert_failure_response(return_code, cdata->id);
      } else {
        char *contentType;
        CURL_EZ_GETINFO(ez_handle, CURLINFO_CONTENT_TYPE, &contentType);

        long http_status_code;
        CURL_EZ_GETINFO(ez_handle, CURLINFO_RESPONSE_CODE, &http_status_code);

        Jsonb *jsonb_headers = jsonb_headers_from_curl_handle(ez_handle);

        insert_success_response(cdata, http_status_code, contentType, jsonb_headers);

        pfree_curl_data(cdata);
      }

      int res = curl_multi_remove_handle(curl_mhandle, ez_handle);
      if(res != CURLM_OK)
        ereport(ERROR, errmsg("curl_multi_remove_handle: %s", curl_multi_strerror(res)));

      curl_easy_cleanup(ez_handle);
    } else {
      ereport(ERROR, errmsg("curl_multi_info_read(), CURLMsg=%d\n", msg->msg));
    }
  }
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

  while (!got_sigterm)
  {
    WaitLatch(&MyProc->procLatch,
          WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
          1000L,
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

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();
    PushActiveSnapshot(GetTransactionSnapshot());
    SPI_connect();

    delete_expired_responses(guc_ttl);

    CURLM *curl_mhandle = curl_multi_init();
    if(!curl_mhandle)
      ereport(ERROR, errmsg("curl_multi_init()"));

    consume_request_queue(curl_mhandle);
    insert_curl_responses(curl_mhandle);

    curl_ret = curl_multi_cleanup(curl_mhandle);
    if(curl_ret != CURLM_OK)
      ereport(ERROR, errmsg("curl_multi_cleanup: %s", curl_multi_strerror(curl_ret)));

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
                "Database where pg_net tables are located",
                NULL,
                &guc_database_name,
                "postgres",
                PGC_SIGHUP, 0,
                NULL, NULL, NULL);
}
