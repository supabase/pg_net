#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "pg_prelude.h"

#include "curl_prelude.h"

#include "core.h"
#include "errors.h"
#include "event.h"

static SPIPlanPtr del_response_plan     = NULL;
static SPIPlanPtr del_return_queue_plan = NULL;
static SPIPlanPtr ins_response_plan     = NULL;

static size_t body_cb(void *contents, size_t size, size_t nmemb, void *userp) {
  CurlHandle *handle   = (CurlHandle *)userp;
  size_t      realsize = size * nmemb;
  appendBinaryStringInfo(handle->body, (const char *)contents, (int)realsize);
  return realsize;
}

// A header with a CR or LF in it can inject extra headers or a body into the request, since libcurl
// ends every header with CRLF. Such requests are not sent. The message only names the header, the
// value may hold credentials.
// Collects the raw header lines of the last response. A status line starts a new response, so a
// redirect's headers are dropped.
static size_t header_cb(char *line, size_t size, size_t nitems, void *userp) {
  CurlHandle *handle   = (CurlHandle *)userp;
  size_t      realsize = size * nitems;
  if (realsize >= 5 && strncmp(line, "HTTP/", 5) == 0) resetStringInfo(handle->resp_headers);
  appendBinaryStringInfo(handle->resp_headers, line, (int)realsize);
  return realsize;
}

typedef struct {
  const char *name;
  size_t      namelen;
  const char *value;
  size_t      valuelen;
} ResponseHeader;

// Advances *cur to the next header field in the collected lines. Status lines, the blank
// terminator and obsolete folded lines are skipped.
static bool next_response_header(StringInfo raw, char **cur, ResponseHeader *out) {
  char *end = raw->data + raw->len;
  while (*cur < end) {
    char *line = *cur;
    char *eol  = memchr(line, '\n', (size_t)(end - line));
    char *stop = eol ? eol : end;
    *cur       = eol ? eol + 1 : end;
    if (stop > line && stop[-1] == '\r') stop--;
    char *colon = memchr(line, ':', (size_t)(stop - line));
    if (!colon || colon == line || *line == ' ' || *line == '\t' || strncmp(line, "HTTP/", 5) == 0)
      continue;
    char *v = colon + 1;
    while (v < stop && (*v == ' ' || *v == '\t'))
      v++;
    char *ve = stop;
    while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t'))
      ve--;
    *out = (ResponseHeader){line, (size_t)(colon - line), v, (size_t)(ve - v)};
    return true;
  }
  return false;
}

static char *crlf_header_rejection(const char *hdr) {
  size_t bad = strcspn(hdr, "\r\n");
  if (hdr[bad] == '\0') return NULL;

  size_t name_len = strcspn(hdr, ":");
  if (name_len > bad) name_len = bad;

  return psprintf("header \"%.*s\" contains a carriage return or line feed", (int)name_len, hdr);
}

static struct curl_slist *pg_text_array_to_slist(ArrayType *array, struct curl_slist *headers,
                                                 char **rejected_reason) {
  ArrayIterator iterator;
  Datum         value;
  bool          isnull;
  char         *hdr;

  iterator = array_create_iterator(array, 0, NULL);

  while (array_iterate(iterator, &value, &isnull)) {
    if (isnull) {
      continue;
    }

    hdr = TextDatumGetCString(value);

    char *reason = crlf_header_rejection(hdr);
    if (reason) {
      if (*rejected_reason == NULL)
        *rejected_reason = reason;
      else
        pfree(reason);
    } else {
      EREPORT_CURL_SLIST_APPEND(headers, hdr);
    }

    pfree(hdr);
  }
  array_free_iterator(iterator);

  return headers;
}

int guc_max_timeout_ms = DEFAULT_MAX_TIMEOUT_MS;

void init_curl_handle(CurlHandle *handle, RequestQueueRow row) {
  handle->id           = row.id;
  handle->body         = makeStringInfo();
  handle->resp_headers = makeStringInfo();
  handle->ez_handle    = curl_easy_init();

  // libcurl treats a 0 timeout as no timeout, and a request that never finishes blocks the batch
  // loop forever. Requests outside the bound are not sent, they get an error response instead. The
  // handle is still fully set up so it can be cleaned up like the others.
  handle->timeout_milliseconds = row.timeout_milliseconds;
  handle->rejected_reason      = NULL;
  if (handle->timeout_milliseconds <= 0 || handle->timeout_milliseconds > guc_max_timeout_ms) {
    handle->rejected_reason =
        psprintf("timeout_milliseconds must be between 1 and %d (pg_net.max_timeout_ms), got %d",
                 guc_max_timeout_ms, row.timeout_milliseconds);
    handle->timeout_milliseconds = guc_max_timeout_ms;
  }

  if (!row.headersBin.isnull) {
    ArrayType         *pgHeaders       = DatumGetArrayTypeP(row.headersBin.value);
    struct curl_slist *request_headers = NULL;

    request_headers = pg_text_array_to_slist(pgHeaders, request_headers, &handle->rejected_reason);

    handle->request_headers = request_headers;
  }

  handle->url = TextDatumGetCString(row.url);

  handle->req_body = !row.bodyBin.isnull ? TextDatumGetCString(row.bodyBin.value) : NULL;

  handle->method = TextDatumGetCString(row.method);

  if (strcasecmp(handle->method, "GET") != 0 && strcasecmp(handle->method, "POST") != 0 &&
      strcasecmp(handle->method, "DELETE") != 0) {
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
    } else {
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
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_HEADERFUNCTION, header_cb);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_HEADERDATA, handle);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_URL, handle->url);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_HTTPHEADER, handle->request_headers);
  // libcurl leaves this out when the request headers have their own User-Agent
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_USERAGENT, "pg_net/" EXTVERSION);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_TIMEOUT_MS, (long)handle->timeout_milliseconds);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_PRIVATE, handle);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_FOLLOWLOCATION, (long)true);
  if (LOG_MIN_MESSAGES <= DEBUG2) EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_VERBOSE, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500 /* libcurl 7.85.0 */
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_PROTOCOLS_STR, "http,https");
#else
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
}

void set_curl_mhandle(CURLM *curl_mhandle) {
  EREPORT_CURL_MULTI_SETOPT(curl_mhandle, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
  EREPORT_CURL_MULTI_SETOPT(curl_mhandle, CURLMOPT_SOCKETDATA, curl_mhandle);
  EREPORT_CURL_MULTI_SETOPT(curl_mhandle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
}

/*
 * Prepares and saves a plan for the given sql source and returns the
 * saved plan
 */
static SPIPlanPtr prepare_and_save_plan(const char *sql, int nargs, Oid *argtypes) {
  SPIPlanPtr prepared = SPI_prepare(sql, nargs, argtypes);
  if (prepared == NULL)
    ereport(ERROR, errmsg("SPI_prepare failed: %s", SPI_result_code_string(SPI_result)));

  SPIPlanPtr saved = SPI_saveplan(prepared);
  if (saved == NULL)
    ereport(ERROR, errmsg("SPI_saveplan failed: %s", SPI_result_code_string(SPI_result)));

  int ret_code = SPI_freeplan(prepared);
  if (ret_code != 0)
    ereport(ERROR, errmsg("SPI_freeplan error: %s", SPI_result_code_string(ret_code)));

  return saved;
}

uint64 delete_expired_responses(char *ttl, int batch_size) {
  if (del_response_plan == NULL) {
    del_response_plan = prepare_and_save_plan("\
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
                                              2, (Oid[]){INTERVALOID, INT4OID});
  }

  int ret_code = SPI_execute_plan(
      del_response_plan,
      (Datum[]){DirectFunctionCall3(interval_in, CStringGetDatum(ttl), ObjectIdGetDatum(InvalidOid),
                                    Int32GetDatum(-1)),
                Int32GetDatum(batch_size)},
      NULL, false, 0);

  uint64 affected_rows = SPI_processed;

  if (ret_code != SPI_OK_DELETE) {
    ereport(ERROR,
            errmsg("Error expiring response table rows: %s", SPI_result_code_string(ret_code)));
  }

  return affected_rows;
}

uint64 consume_request_queue(const int batch_size) {
  if (del_return_queue_plan == NULL) {
    del_return_queue_plan = prepare_and_save_plan("\
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
                                                  1, (Oid[]){INT4OID});
  }

  int ret_code =
      SPI_execute_plan(del_return_queue_plan, (Datum[]){Int32GetDatum(batch_size)}, NULL, false, 0);

  if (ret_code != SPI_OK_DELETE_RETURNING)
    ereport(ERROR,
            errmsg("Error getting http request queue: %s", SPI_result_code_string(ret_code)));

  return SPI_processed;
}

// This has an implicit dependency on the execution of
// delete_return_request_queue, unfortunately we're not able to make this
// dependency explicit due to the design of SPI (which uses global variables)
RequestQueueRow get_request_queue_row(HeapTuple spi_tupval, TupleDesc spi_tupdesc) {
  bool tupIsNull = false;

  int64 id = DatumGetInt64(SPI_getbinval(spi_tupval, spi_tupdesc, 1, &tupIsNull));
  EREPORT_NULL_ATTR(tupIsNull, id);

  Datum method = SPI_getbinval(spi_tupval, spi_tupdesc, 2, &tupIsNull);
  EREPORT_NULL_ATTR(tupIsNull, method);

  Datum url = SPI_getbinval(spi_tupval, spi_tupdesc, 3, &tupIsNull);
  EREPORT_NULL_ATTR(tupIsNull, url);

  int32 timeout_milliseconds = DatumGetInt32(SPI_getbinval(spi_tupval, spi_tupdesc, 4, &tupIsNull));
  EREPORT_NULL_ATTR(tupIsNull, timeout_milliseconds);

  NullableDatum headersBin = {.value  = SPI_getbinval(spi_tupval, spi_tupdesc, 5, &tupIsNull),
                              .isnull = tupIsNull};

  NullableDatum bodyBin = {.value  = SPI_getbinval(spi_tupval, spi_tupdesc, 6, &tupIsNull),
                           .isnull = tupIsNull};

  return (RequestQueueRow){id, method, url, timeout_milliseconds, headersBin, bodyBin};
}

static Jsonb *jsonb_headers_from_curl_handle(CurlHandle *handle) {
  PG_JSONB_INIT_STATE(headers);
  (void)PG_JSONB_PUSH(headers, WJB_BEGIN_OBJECT, NULL);

  char          *cur = handle->resp_headers->data;
  ResponseHeader h;
  while (next_response_header(handle->resp_headers, &cur, &h)) {
    JsonbValue key = {
      .type = jbvString,
      .val  = {.string = {.val = pnstrdup(h.name, h.namelen), .len = (int)h.namelen}}};
    JsonbValue value = {
      .type = jbvString,
      .val  = {.string = {.val = pnstrdup(h.value, h.valuelen), .len = (int)h.valuelen}}};
    (void)PG_JSONB_PUSH(headers, WJB_KEY, &key);
    (void)PG_JSONB_PUSH(headers, WJB_VALUE, &value);
  }

  return PG_JSONB_OBJECT_FINISH(headers);
}

static char *content_type_from_handle(CurlHandle *handle) {
  char          *cur = handle->resp_headers->data;
  ResponseHeader h;
  while (next_response_header(handle->resp_headers, &cur, &h))
    if (h.namelen == 12 && pg_strncasecmp(h.name, "content-type", 12) == 0)
      return pnstrdup(h.value, h.valuelen);
  return NULL;
}

enum { response_nparams = 7 }; // using an enum because const size_t doesn't compile

static void execute_insert_response(Datum *vals, char *nulls) {
  if (ins_response_plan == NULL) {
    ins_response_plan = prepare_and_save_plan(
        "\
        insert into net._http_response(id, status_code, content, headers, content_type, timed_out, error_msg) values ($1, $2, $3, $4, $5, $6, $7)",
        response_nparams,
        (Oid[response_nparams]){INT8OID, INT4OID, TEXTOID, JSONBOID, TEXTOID, BOOLOID, TEXTOID});
  }

  int ret_code = SPI_execute_plan(ins_response_plan, vals, nulls, false, 0);

  if (ret_code != SPI_OK_INSERT) {
    ereport(ERROR, errmsg("Error when inserting response: %s", SPI_result_code_string(ret_code)));
  }
}

void insert_rejected_response(CurlHandle *handle) {
  Datum vals[response_nparams];
  char  nulls[response_nparams];
  MemSet(nulls, 'n', response_nparams);

  vals[0]  = Int64GetDatum(handle->id);
  nulls[0] = ' ';
  vals[5]  = BoolGetDatum(false);
  nulls[5] = ' ';
  vals[6]  = CStringGetTextDatum(handle->rejected_reason);
  nulls[6] = ' ';

  execute_insert_response(vals, nulls);
}

void insert_response(CurlHandle *handle, CURLcode curl_return_code) {
  enum { nparams = response_nparams };
  Datum vals[nparams];
  char  nulls[nparams];
  MemSet(nulls, 'n', nparams);

  vals[0]  = Int64GetDatum(handle->id);
  nulls[0] = ' ';

  if (curl_return_code == CURLE_OK) {
    Jsonb *jsonb_headers        = jsonb_headers_from_curl_handle(handle);
    long   res_http_status_code = 0;

    EREPORT_CURL_GETINFO(handle->ez_handle, CURLINFO_RESPONSE_CODE, &res_http_status_code);

    vals[1]  = Int32GetDatum(res_http_status_code);
    nulls[1] = ' ';

    if (handle->body && handle->body->data[0] != '\0') {
      vals[2]  = CStringGetTextDatum(handle->body->data);
      nulls[2] = ' ';
    }

    vals[3]  = JsonbPGetDatum(jsonb_headers);
    nulls[3] = ' ';

    char *content_type = content_type_from_handle(handle);
    if (content_type) {
      vals[4]  = CStringGetTextDatum(content_type);
      nulls[4] = ' ';
    }

    vals[5]  = BoolGetDatum(false);
    nulls[5] = ' ';
  } else {
    bool timed_out = curl_return_code == CURLE_OPERATION_TIMEDOUT;

    vals[5]  = BoolGetDatum(timed_out);
    nulls[5] = ' ';

    if (timed_out) {
      curl_timeout_msg timeout_msg =
          detailed_timeout_strerror(handle->ez_handle, handle->timeout_milliseconds);

      vals[6]  = CStringGetTextDatum(timeout_msg.msg);
      nulls[6] = ' ';
    } else {
      const char *error_msg = curl_easy_strerror(curl_return_code);

      if (error_msg) {
        vals[6]  = CStringGetTextDatum(error_msg);
        nulls[6] = ' ';
      }
    }
  }

  execute_insert_response(vals, nulls);
}

void pfree_handle(CurlHandle *handle) {
  pfree(handle->url);
  pfree(handle->method);
  if (handle->req_body) pfree(handle->req_body);
  if (handle->rejected_reason) pfree(handle->rejected_reason);

  if (handle->body) destroyStringInfo(handle->body);
  if (handle->resp_headers) destroyStringInfo(handle->resp_headers);

  if (handle->request_headers) // curl_slist_free_all already handles the NULL
                               // case, but be explicit about it
    curl_slist_free_all(handle->request_headers);
}
