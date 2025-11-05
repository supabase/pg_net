#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include "pg_prelude.h"
#include "curl_prelude.h"
#include "core.h"
#include "event.h"
#include "errors.h"

static SPIPlanPtr del_response_plan     = NULL;
static SPIPlanPtr del_return_queue_plan = NULL;
static SPIPlanPtr ins_response_plan     = NULL;

static size_t
body_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
  CurlHandle *handle = (CurlHandle*) userp;
  size_t realsize = size * nmemb;
  appendBinaryStringInfo(handle->body, (const char*)contents, (int)realsize);
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

void init_curl_handle(CurlHandle *handle, RequestQueueRow row){
  handle->id   = row.id;
  handle->body = makeStringInfo();
  handle->ez_handle = curl_easy_init();

  handle->timeout_milliseconds = row.timeout_milliseconds;

  if (!row.headersBin.isnull) {
    ArrayType *pgHeaders = DatumGetArrayTypeP(row.headersBin.value);
    struct curl_slist *request_headers = NULL;

    request_headers = pg_text_array_to_slist(pgHeaders, request_headers);

    EREPORT_CURL_SLIST_APPEND(request_headers, "User-Agent: pg_net/" EXTVERSION);

    handle->request_headers = request_headers;
  }

  handle->url = TextDatumGetCString(row.url);

  handle->req_body = !row.bodyBin.isnull ? TextDatumGetCString(row.bodyBin.value) : NULL;

  handle->method = TextDatumGetCString(row.method);

  if (strcasecmp(handle->method, "GET") != 0 && strcasecmp(handle->method, "POST") != 0 && strcasecmp(handle->method, "DELETE") != 0) {
    ereport(ERROR, errmsg("Unsupported request method %s", handle->method));
  }

  if (strcasecmp(handle->method, "GET") == 0) {
    if (handle->req_body) {
      EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_POSTFIELDS, handle->req_body);
      EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_CUSTOMREQUEST, "GET");
    }
  }

  if (strcasecmp(handle->method, "POST") == 0) {
    if (handle->req_body) {
      EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_POSTFIELDS, handle->req_body);
    }
    else {
      EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_POST, 1L);
      EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_POSTFIELDSIZE, 0L);
    }
  }

  if (strcasecmp(handle->method, "DELETE") == 0) {
    EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (handle->req_body) {
      EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_POSTFIELDS, handle->req_body);
    }
  }

  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_WRITEFUNCTION, body_cb);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_WRITEDATA, handle);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_HEADER, 0L);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_URL, handle->url);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_HTTPHEADER, handle->request_headers);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_TIMEOUT_MS, (long) handle->timeout_milliseconds);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_PRIVATE, handle);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_FOLLOWLOCATION, (long) true);
  if (log_min_messages <= DEBUG2)
    EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_VERBOSE, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500 /* libcurl 7.85.0 */
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_PROTOCOLS_STR, "http,https");
#else
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
}

void set_curl_mhandle(WorkerState *wstate){
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_SOCKETDATA, wstate);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_TIMERDATA, wstate);
}

uint64 delete_expired_responses(char *ttl, int batch_size){
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

  return affected_rows;
}

uint64 consume_request_queue(const int batch_size){
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

  return SPI_processed;
}

// This has an implicit dependency on the execution of delete_return_request_queue,
// unfortunately we're not able to make this dependency explicit
// due to the design of SPI (which uses global variables)
RequestQueueRow get_request_queue_row(HeapTuple spi_tupval, TupleDesc spi_tupdesc){
  bool tupIsNull = false;

  int64 id = DatumGetInt64(SPI_getbinval(spi_tupval, spi_tupdesc, 1, &tupIsNull));
  EREPORT_NULL_ATTR(tupIsNull, id);

  Datum method = SPI_getbinval(spi_tupval, spi_tupdesc, 2, &tupIsNull);
  EREPORT_NULL_ATTR(tupIsNull, method);

  Datum url = SPI_getbinval(spi_tupval, spi_tupdesc, 3, &tupIsNull);
  EREPORT_NULL_ATTR(tupIsNull, url);

  int32 timeout_milliseconds = DatumGetInt32(SPI_getbinval(spi_tupval, spi_tupdesc, 4, &tupIsNull));
  EREPORT_NULL_ATTR(tupIsNull, timeout_milliseconds);

  NullableDatum headersBin = {
    .value = SPI_getbinval(spi_tupval, spi_tupdesc, 5, &tupIsNull),
    .isnull = tupIsNull
  };

  NullableDatum bodyBin = {
    .value = SPI_getbinval(spi_tupval, spi_tupdesc, 6, &tupIsNull),
    .isnull = tupIsNull
  };

  return (RequestQueueRow){
    id, method, url, timeout_milliseconds, headersBin, bodyBin
  };
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

void insert_response(CurlHandle *handle, CURLcode curl_return_code){
  enum { nparams = 7 }; // using an enum because const size_t nparams doesn't compile
  Datum vals[nparams];
  char  nulls[nparams]; MemSet(nulls, 'n', nparams);

  vals[0] = Int64GetDatum(handle->id);
  nulls[0] = ' ';

  if (curl_return_code == CURLE_OK) {
    Jsonb *jsonb_headers = jsonb_headers_from_curl_handle(handle->ez_handle);
    long res_http_status_code = 0;

    EREPORT_CURL_GETINFO(handle->ez_handle, CURLINFO_RESPONSE_CODE, &res_http_status_code);

    vals[1] = Int32GetDatum(res_http_status_code);
    nulls[1] = ' ';

    if (handle->body && handle->body->data[0] != '\0'){
      vals[2] = CStringGetTextDatum(handle->body->data);
      nulls[2] = ' ';
    }

    vals[3] = JsonbPGetDatum(jsonb_headers);
    nulls[3] = ' ';

    struct curl_header *hdr;
    if (curl_easy_header(handle->ez_handle, "content-type", 0, CURLH_HEADER, -1, &hdr) == CURLHE_OK){
      vals[4] = CStringGetTextDatum(hdr->value);
      nulls[4] = ' ';
    }

    vals[5] = BoolGetDatum(false);
    nulls[5] = ' ';
  } else {
    bool timed_out = curl_return_code == CURLE_OPERATION_TIMEDOUT;
    char *error_msg = NULL;

    if (timed_out){
      error_msg = detailed_timeout_strerror(handle->ez_handle, handle->timeout_milliseconds).msg;
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

void pfree_handle(CurlHandle *handle){
  pfree(handle->url);
  pfree(handle->method);
  if(handle->req_body)
    pfree(handle->req_body);

  if(handle->body)
    destroyStringInfo(handle->body);

  if(handle->request_headers) //curl_slist_free_all already handles the NULL case, but be explicit about it
    curl_slist_free_all(handle->request_headers);
}
