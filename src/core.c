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

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include "util.h"
#include "core.h"
#include "event.h"

typedef struct {
  int64 id;
  StringInfo body;
  struct curl_slist* request_headers;
  int32 timeout_milliseconds;
} CurlData;

typedef struct {
  char msg[256];
} curl_timeout_msg;

static size_t
body_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
  CurlData *cdata = (CurlData*) userp;
  size_t realsize = size * nmemb;
  appendBinaryStringInfo(cdata->body, (const char*)contents, (int)realsize);
  return realsize;
}

// We need a different memory context here, as the parent function will have an SPI memory context, which has a shorter lifetime.
static void init_curl_handle(CURLM *curl_mhandle, MemoryContext curl_memctx, int64 id, Datum urlBin, NullableDatum bodyBin, NullableDatum headersBin, Datum methodBin, int32 timeout_milliseconds){
  MemoryContext old_ctx = MemoryContextSwitchTo(curl_memctx);

  CurlData *cdata = palloc(sizeof(CurlData));
  cdata->id   = id;
  cdata->body = makeStringInfo();

  cdata->timeout_milliseconds = timeout_milliseconds;

  if (!headersBin.isnull) {
    ArrayType *pgHeaders = DatumGetArrayTypeP(headersBin.value);
    struct curl_slist *request_headers = NULL;

    request_headers = pg_text_array_to_slist(pgHeaders, request_headers);

    CURL_SLIST_APPEND(request_headers, "User-Agent: pg_net/" EXTVERSION);

    cdata->request_headers = request_headers;
  }

  char *url = TextDatumGetCString(urlBin);

  char *reqBody = !bodyBin.isnull ? TextDatumGetCString(bodyBin.value) : NULL;

  char *method = TextDatumGetCString(methodBin);
  if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "POST") != 0 && strcasecmp(method, "DELETE") != 0) {
    ereport(ERROR, errmsg("Unsupported request method %s", method));
  }

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
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_WRITEDATA, cdata);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_HEADER, 0L);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_URL, url);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_HTTPHEADER, cdata->request_headers);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_TIMEOUT_MS, cdata->timeout_milliseconds);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_PRIVATE, cdata);
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_FOLLOWLOCATION, true);
  if (log_min_messages <= DEBUG2)
    CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_VERBOSE, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500 /* libcurl 7.85.0 */
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_PROTOCOLS_STR, "http,https");
#else
  CURL_EZ_SETOPT(curl_ez_handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

  EREPORT_MULTI(
    curl_multi_add_handle(curl_mhandle, curl_ez_handle)
  );

  MemoryContextSwitchTo(old_ctx);
}

void set_curl_mhandle(CURLM *curl_mhandle, LoopState *lstate){
  CURL_MULTI_SETOPT(curl_mhandle, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
  CURL_MULTI_SETOPT(curl_mhandle, CURLMOPT_SOCKETDATA, lstate);
  CURL_MULTI_SETOPT(curl_mhandle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  CURL_MULTI_SETOPT(curl_mhandle, CURLMOPT_TIMERDATA, lstate);
}

void delete_expired_responses(char *ttl, int batch_size){
  SetCurrentStatementStartTimestamp();
  StartTransactionCommand();
  PushActiveSnapshot(GetTransactionSnapshot());
  SPI_connect();

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
    , Int32GetDatum(batch_size)
    }, NULL, false, 0);

  if (ret_code != SPI_OK_DELETE)
  {
    ereport(ERROR, errmsg("Error expiring response table rows: %s", SPI_result_code_string(ret_code)));
  }

  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
}


/*
 * Show a more detailed error message when a timeout happens, which includes the DNS, TCP/SSL handshake and HTTP request/response time. An example message is like:
 *
 * "Timeout of 800 ms reached. Total time: 801.159000 ms (DNS time: 73.407000 ms, TCP/SSL handshake time: 677.256000 ms, HTTP Request/Respose time: 50.103000 ms)"
 *
 * Curl allows to calculate the above by applying substractions on some internal timings. Refer to https://blog.cloudflare.com/a-question-of-timing/ for an explanation of these timings.
 *
 * There are extra considerations:
 *
 * - If a step surpasses the request timeout, say the TCP handshake (CURLINFO_CONNECT_TIME), its given timing is 0.
 *   However the TCP handshake duration can still be determined by using the total time (CURLINFO_TOTAL_TIME).
 *   We want to show at which phase the timeout occurred.
 *
 * - If a step is omitted, say an SSL handshake (CURLINFO_APPCONNECT_TIME) on non-HTTPS requests, its given timing is 0.
 *
 * - The pretransfer time (CURLINFO_PRETRANSFER_TIME) is greater than 0 when the HTTP request step starts.
 */
static curl_timeout_msg detailed_timeout_strerror(CURL *ez_handle, int32 timeout_milliseconds){
  double namelookup;    CURL_EZ_GETINFO(ez_handle, CURLINFO_NAMELOOKUP_TIME,    &namelookup);
  double appconnect;    CURL_EZ_GETINFO(ez_handle, CURLINFO_APPCONNECT_TIME,    &appconnect);
  double connect;       CURL_EZ_GETINFO(ez_handle, CURLINFO_CONNECT_TIME,       &connect);
  double pretransfer;   CURL_EZ_GETINFO(ez_handle, CURLINFO_PRETRANSFER_TIME,   &pretransfer);
  double starttransfer; CURL_EZ_GETINFO(ez_handle, CURLINFO_STARTTRANSFER_TIME, &starttransfer);
  double total;         CURL_EZ_GETINFO(ez_handle, CURLINFO_TOTAL_TIME,         &total);

  elog(DEBUG2, "The curl timings are time_namelookup: %f, time_connect: %f, time_appconnect: %f, time_pretransfer: %f, time_starttransfer: %f, time_total: %f",
      namelookup, connect, appconnect, pretransfer, starttransfer, total);

  // Steps at which the request timed out
  bool timedout_at_dns       = namelookup == 0 && connect == 0; // if DNS time is 0 and no TCP occurred, it timed out at the DNS step
  bool timedout_at_handshake = pretransfer == 0; // pretransfer determines if the HTTP step started, if 0 no HTTP ocurred and thus the timeout occurred at TCP or SSL handshake step
  bool timedout_at_http      = pretransfer > 0; // The HTTP step did start and the timeout occurred here

  // Calculate the steps times
  double _dns_time =
    timedout_at_dns ?
      total: // get the total since namelookup will be 0 because of the timeout
    timedout_at_handshake ?
      namelookup:
    timedout_at_http ?
      namelookup:
    0;

  double _handshake_time =
    timedout_at_dns ?
      0:
    timedout_at_handshake ?
      total - namelookup: // connect or appconnect will be 0 because of the timeout, get the total - DNS step time
    timedout_at_http ?
      (connect - namelookup) +                    // TCP handshake time
      (appconnect > 0 ? (appconnect - connect): 0): // SSL handshake time. Prevent a negative here which can happen when no SSL is involved (plain HTTP request) and appconnect is 0
    0;

  double _http_time =
    timedout_at_dns ?
      0:
    timedout_at_handshake ?
      0:
    timedout_at_http ?
      total - pretransfer:
    0;

  // convert seconds to milliseconds
  double dns_time_ms       = _dns_time       * 1000;
  double handshake_time_ms = _handshake_time * 1000;
  double http_time_ms      = _http_time      * 1000;
  double total_time_ms     = total           * 1000;

  // build the error message
  curl_timeout_msg result = {.msg = {}};
  sprintf(result.msg,
    "Timeout of %d ms reached. Total time: %f ms (DNS time: %f ms, TCP/SSL handshake time: %f ms, HTTP Request/Response time: %f ms)",
    timeout_milliseconds, total_time_ms, dns_time_ms, handshake_time_ms, http_time_ms
  );
  return result;
}

static void insert_failure_response(CURL *ez_handle, CURLcode return_code, int64 id, int32 timeout_milliseconds){
  StartTransactionCommand();
  PushActiveSnapshot(GetTransactionSnapshot());
  SPI_connect();

  const char* error_msg;
  if (return_code == CURLE_OPERATION_TIMEDOUT){
    error_msg = detailed_timeout_strerror(ez_handle, timeout_milliseconds).msg;
  } else {
    error_msg = curl_easy_strerror(return_code);
  }

  int ret_code = SPI_execute_with_args("\
      insert into net._http_response(id, error_msg) values ($1, $2)",
      2,
      (Oid[]){INT8OID, CSTRINGOID},
      (Datum[]){Int64GetDatum(id), CStringGetDatum(error_msg)},
      NULL, false, 1);

  if (ret_code != SPI_OK_INSERT)
  {
    ereport(ERROR, errmsg("Error when inserting failed response: %s", SPI_result_code_string(ret_code)));
  }

  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
}

static void insert_success_response(CurlData *cdata, long http_status_code, char *contentType, Jsonb *jsonb_headers){
  StartTransactionCommand();
  PushActiveSnapshot(GetTransactionSnapshot());
  SPI_connect();

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
      , BoolGetDatum(false) // timed_out is false here as it's a success
      },
      (char[6]){
        ' '
      , [2] = cdata->body->data[0] == '\0'? 'n' : ' '
      , [4] = !contentType? 'n' :' '
      },
      false, 1);

  if (ret_code != SPI_OK_INSERT)
  {
    ereport(ERROR, errmsg("Error when inserting successful response: %s", SPI_result_code_string(ret_code)));
  }

  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
}

void consume_request_queue(CURLM *curl_mhandle, int batch_size, MemoryContext curl_memctx){
  StartTransactionCommand();
  PushActiveSnapshot(GetTransactionSnapshot());
  SPI_connect();

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
    (Datum[]){Int32GetDatum(batch_size)},
    NULL, false, 0);

  if (ret_code != SPI_OK_DELETE_RETURNING)
    ereport(ERROR, errmsg("Error getting http request queue: %s", SPI_result_code_string(ret_code)));


  for (int j = 0; j < SPI_processed; j++) {
    bool tupIsNull = false;

    int64 id = DatumGetInt64(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 1, &tupIsNull));
    EREPORT_NULL_ATTR(tupIsNull, id);

    int32 timeout_milliseconds = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 4, &tupIsNull));
    EREPORT_NULL_ATTR(tupIsNull, timeout_milliseconds);

    Datum method = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 2, &tupIsNull);
    EREPORT_NULL_ATTR(tupIsNull, method);

    Datum url = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 3, &tupIsNull);
    EREPORT_NULL_ATTR(tupIsNull, url);

    NullableDatum headersBin = {
      .value = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 5, &tupIsNull),
      .isnull = tupIsNull
    };

    NullableDatum bodyBin = {
      .value = SPI_getbinval(SPI_tuptable->vals[j], SPI_tuptable->tupdesc, 6, &tupIsNull),
      .isnull = tupIsNull
    };

    init_curl_handle(curl_mhandle, curl_memctx, id, url, bodyBin, headersBin, method, timeout_milliseconds);
  }

  SPI_finish();
  PopActiveSnapshot();
  CommitTransactionCommand();
}

static void pfree_curl_data(CurlData *cdata){
  pfree(cdata->body->data);
  pfree(cdata->body);
  if(cdata->request_headers) //curl_slist_free_all already handles the NULL case, but be explicit about it
    curl_slist_free_all(cdata->request_headers);
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

// Switch back to the curl memory context, which has the curl handles stored
void insert_curl_responses(LoopState *lstate, MemoryContext curl_memctx){
  MemoryContext old_ctx = MemoryContextSwitchTo(curl_memctx);
  int msgs_left=0;
  CURLMsg *msg = NULL;
  CURLM *curl_mhandle = lstate->curl_mhandle;

  while ((msg = curl_multi_info_read(curl_mhandle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURLcode return_code = msg->data.result;
      CURL *ez_handle= msg->easy_handle;
      CurlData *cdata = NULL;
      CURL_EZ_GETINFO(ez_handle, CURLINFO_PRIVATE, &cdata);

      if (return_code != CURLE_OK) {
        insert_failure_response(ez_handle, return_code, cdata->id, cdata->timeout_milliseconds);
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

  MemoryContextSwitchTo(old_ctx);
}
