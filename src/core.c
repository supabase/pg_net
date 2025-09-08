#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include "pg_prelude.h"
#include "curl_prelude.h"
#include "core.h"
#include "event.h"
#include "errors.h"

typedef struct {
  int64 id;
  StringInfo body;
  struct curl_slist* request_headers;
  int32 timeout_milliseconds;
} CurlData;

static SPIPlanPtr del_response_plan     = NULL;
static SPIPlanPtr del_return_queue_plan = NULL;
static SPIPlanPtr ins_response_plan     = NULL;

static size_t
body_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
  CurlData *cdata = (CurlData*) userp;
  size_t realsize = size * nmemb;
  appendBinaryStringInfo(cdata->body, (const char*)contents, (int)realsize);
  return realsize;
}

static struct curl_slist *pg_text_array_to_slist(ArrayType *array,
                                          struct curl_slist *headers) {
    ArrayIterator iterator;
    Datum value;
    bool isnull;
    char *hdr;

    iterator = array_create_iterator(array, 0, NULL);

    while (array_iterate(iterator, &value, &isnull)) {
        if (isnull) {
            continue;
        }

        hdr = TextDatumGetCString(value);
        EREPORT_CURL_SLIST_APPEND(headers, hdr);
        pfree(hdr);
    }
    array_free_iterator(iterator);

    return headers;
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

    EREPORT_CURL_SLIST_APPEND(request_headers, "User-Agent: pg_net/" EXTVERSION);

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
      EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_POSTFIELDS, reqBody);
      EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_CUSTOMREQUEST, "GET");
    }
  }

  if (strcasecmp(method, "POST") == 0) {
    if (reqBody) {
      EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_POSTFIELDS, reqBody);
    }
    else {
      EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_POST, 1L);
      EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_POSTFIELDSIZE, 0L);
    }
  }

  if (strcasecmp(method, "DELETE") == 0) {
    EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (reqBody) {
      EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_POSTFIELDS, reqBody);
    }
  }

  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_WRITEFUNCTION, body_cb);
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_WRITEDATA, cdata);
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_HEADER, 0L);
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_URL, url);
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_HTTPHEADER, cdata->request_headers);
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_TIMEOUT_MS, (long) cdata->timeout_milliseconds);
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_PRIVATE, cdata);
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_FOLLOWLOCATION, (long) true);
  if (log_min_messages <= DEBUG2)
    EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_VERBOSE, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500 /* libcurl 7.85.0 */
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_PROTOCOLS_STR, "http,https");
#else
  EREPORT_CURL_SETOPT(curl_ez_handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

  EREPORT_MULTI(
    curl_multi_add_handle(curl_mhandle, curl_ez_handle)
  );

  MemoryContextSwitchTo(old_ctx);
}

void set_curl_mhandle(WorkerState *wstate){
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_SOCKETDATA, wstate);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_TIMERDATA, wstate);
}

uint64 delete_expired_responses(char *ttl, int batch_size){
  SPI_connect();

  if (del_response_plan == NULL) {
      SPIPlanPtr tmp = SPI_prepare("\
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
        (Oid[]){INTERVALOID, INT4OID});
      if (tmp == NULL)
          ereport(ERROR, errmsg("SPI_prepare failed: %s", SPI_result_code_string(SPI_result)));

      del_response_plan = SPI_saveplan(tmp);
      if (del_response_plan == NULL)
          ereport(ERROR, errmsg("SPI_saveplan failed"));
  }

  int ret_code = SPI_execute_plan(del_response_plan,
    (Datum[]){
      DirectFunctionCall3(interval_in, CStringGetDatum(ttl), ObjectIdGetDatum(InvalidOid), Int32GetDatum(-1))
    , Int32GetDatum(batch_size)
    }, NULL, false, 0);

  uint64 affected_rows = SPI_processed;

  if (ret_code != SPI_OK_DELETE)
  {
    ereport(ERROR, errmsg("Error expiring response table rows: %s", SPI_result_code_string(ret_code)));
  }

  SPI_finish();

  return affected_rows;
}

uint64 consume_request_queue(CURLM *curl_mhandle, int batch_size, MemoryContext curl_memctx){
  SPI_connect();

  if (del_return_queue_plan == NULL) {
      SPIPlanPtr tmp = SPI_prepare("\
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
        (Oid[]){INT4OID});

      if (tmp == NULL)
          ereport(ERROR, errmsg("SPI_prepare failed: %s", SPI_result_code_string(SPI_result)));

      del_return_queue_plan = SPI_saveplan(tmp);
      if (del_return_queue_plan == NULL)
          ereport(ERROR, errmsg("SPI_saveplan failed"));
  }

  int ret_code = SPI_execute_plan(del_return_queue_plan, (Datum[]){Int32GetDatum(batch_size)}, NULL, false, 0);

  if (ret_code != SPI_OK_DELETE_RETURNING)
    ereport(ERROR, errmsg("Error getting http request queue: %s", SPI_result_code_string(ret_code)));

  uint64 affected_rows = SPI_processed;

  for (size_t j = 0; j < affected_rows; j++) {
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

  return affected_rows;
}

static void pfree_curl_data(CurlData *cdata){
  if(cdata->body){
    destroyStringInfo(cdata->body);
  }
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

static void insert_response(CURL *ez_handle, CurlData *cdata, CURLcode curl_return_code){
  enum { nparams = 7 }; // using an enum because const size_t nparams doesn't compile
  Datum vals[nparams];
  char  nulls[nparams]; MemSet(nulls, 'n', nparams);

  vals[0] = Int64GetDatum(cdata->id);
  nulls[0] = ' ';

  if (curl_return_code == CURLE_OK) {
    Jsonb *jsonb_headers = jsonb_headers_from_curl_handle(ez_handle);
    long res_http_status_code = 0;

    EREPORT_CURL_GETINFO(ez_handle, CURLINFO_RESPONSE_CODE, &res_http_status_code);

    vals[1] = Int32GetDatum(res_http_status_code);
    nulls[1] = ' ';

    if (cdata->body && cdata->body->data[0] != '\0'){
      vals[2] = CStringGetTextDatum(cdata->body->data);
      nulls[2] = ' ';
    }

    vals[3] = JsonbPGetDatum(jsonb_headers);
    nulls[3] = ' ';

    struct curl_header *hdr;
    if (curl_easy_header(ez_handle, "content-type", 0, CURLH_HEADER, -1, &hdr) == CURLHE_OK){
      vals[4] = CStringGetTextDatum(hdr->value);
      nulls[4] = ' ';
    }

    vals[5] = BoolGetDatum(false);
    nulls[5] = ' ';
  } else {
    bool timed_out = curl_return_code == CURLE_OPERATION_TIMEDOUT;
    char *error_msg = NULL;

    if (timed_out){
      error_msg = detailed_timeout_strerror(ez_handle, cdata->timeout_milliseconds).msg;
    } else {
      error_msg = (char *) curl_easy_strerror(curl_return_code);
    }

    vals[5] = BoolGetDatum(timed_out);
    nulls[5] = ' ';

    if (error_msg){
      vals[6] = CStringGetTextDatum(error_msg);
      nulls[6] = ' ';
    }
  }

  if (ins_response_plan == NULL) {
      SPIPlanPtr tmp = SPI_prepare("\
        insert into net._http_response(id, status_code, content, headers, content_type, timed_out, error_msg) values ($1, $2, $3, $4, $5, $6, $7)",
        nparams,
        (Oid[nparams]){INT8OID, INT4OID, TEXTOID, JSONBOID, TEXTOID, BOOLOID, TEXTOID});

      if (tmp == NULL)
          ereport(ERROR, errmsg("SPI_prepare failed: %s", SPI_result_code_string(SPI_result)));

      ins_response_plan = SPI_saveplan(tmp);
      if (ins_response_plan == NULL)
          ereport(ERROR, errmsg("SPI_saveplan failed"));

      SPI_freeplan(tmp);
  }

  int ret_code = SPI_execute_plan(ins_response_plan, vals, nulls, false, 0);

  if (ret_code != SPI_OK_INSERT)
  {
    ereport(ERROR, errmsg("Error when inserting response: %s", SPI_result_code_string(ret_code)));
  }
}

// Switch back to the curl memory context, which has the curl handles stored
void insert_curl_responses(WorkerState *wstate, MemoryContext curl_memctx){
  MemoryContext old_ctx = MemoryContextSwitchTo(curl_memctx);
  int msgs_left=0;
  CURLMsg *msg = NULL;
  CURLM *curl_mhandle = wstate->curl_mhandle;

  while ((msg = curl_multi_info_read(curl_mhandle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      CURLcode return_code = msg->data.result;
      CURL *ez_handle= msg->easy_handle;
      CurlData *cdata = NULL;
      EREPORT_CURL_GETINFO(ez_handle, CURLINFO_PRIVATE, &cdata);

      SPI_connect();
      insert_response(ez_handle, cdata, return_code);
      SPI_finish();

      pfree_curl_data(cdata);

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

