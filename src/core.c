#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "pg_prelude.h"

#include "curl_prelude.h"

#include "core.h"
#include "errors.h"
#include "event.h"

static SPIPlanPtr del_response_plan            = NULL;
static SPIPlanPtr del_return_queue_plan        = NULL;
static SPIPlanPtr del_return_queue_plan_legacy = NULL;
static SPIPlanPtr ins_response_plan            = NULL;

static size_t body_cb(void *contents, size_t size, size_t nmemb, void *userp) {
  CurlHandle *handle   = (CurlHandle *)userp;
  size_t      realsize = size * nmemb;
  appendBinaryStringInfo(handle->body, (const char *)contents, (int)realsize);
  return realsize;
}

// discards the response body without buffering it, used when store_response is false
static size_t discard_cb(__attribute__((unused)) void *contents, size_t size, size_t nmemb,
                         __attribute__((unused)) void *userp) {
  return size * nmemb;
}

static struct curl_slist *pg_text_array_to_slist(ArrayType *array, struct curl_slist *headers) {
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
    EREPORT_CURL_SLIST_APPEND(headers, hdr);
    pfree(hdr);
  }
  array_free_iterator(iterator);

  return headers;
}

void init_curl_handle(CurlHandle *handle, RequestQueueRow row) {
  handle->id            = row.id;
  handle->use_callbacks = row.use_callbacks;
  handle->on_success =
      !row.onSuccessBin.isnull ? TextDatumGetCString(row.onSuccessBin.value) : NULL;
  handle->on_error = !row.onErrorBin.isnull ? TextDatumGetCString(row.onErrorBin.value) : NULL;
  handle->calling_role =
      !row.callingRoleBin.isnull ? TextDatumGetCString(row.callingRoleBin.value) : NULL;

  // the body is only needed when it ends up in net._http_response or in the
  // on_success callback, otherwise don't buffer it
  bool body_needed = !handle->use_callbacks || handle->on_success != NULL;
  handle->body     = body_needed ? makeStringInfo() : NULL;

  handle->ez_handle = curl_easy_init();

  handle->timeout_milliseconds = row.timeout_milliseconds;

  if (!row.headersBin.isnull) {
    ArrayType         *pgHeaders       = DatumGetArrayTypeP(row.headersBin.value);
    struct curl_slist *request_headers = NULL;

    request_headers = pg_text_array_to_slist(pgHeaders, request_headers);

    EREPORT_CURL_SLIST_APPEND(request_headers, "User-Agent: pg_net/" EXTVERSION);

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

  if (handle->body) {
    EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_WRITEFUNCTION, body_cb);
    EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_WRITEDATA, handle);
  } else {
    EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_WRITEFUNCTION, discard_cb);
  }
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_HEADER, 0L);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_URL, handle->url);
  EREPORT_CURL_SETOPT(handle->ez_handle, CURLOPT_HTTPHEADER, handle->request_headers);
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

void set_curl_mhandle(WorkerState *wstate) {
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_SOCKETDATA, wstate);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  EREPORT_CURL_MULTI_SETOPT(wstate->curl_mhandle, CURLMOPT_TIMERDATA, wstate);
}

uint64 delete_expired_responses(char *ttl, int batch_size) {
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
                                 2, (Oid[]){INTERVALOID, INT4OID});
    if (tmp == NULL)
      ereport(ERROR, errmsg("SPI_prepare failed: %s", SPI_result_code_string(SPI_result)));

    del_response_plan = SPI_saveplan(tmp);
    if (del_response_plan == NULL) ereport(ERROR, errmsg("SPI_saveplan failed"));
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

uint64 consume_request_queue(const int batch_size, Oid queue_oid) {
  /*
   * The callback columns only exist from extension version 0.21.0 onwards.
   * The loaded binary can be newer than the installed extension version (the
   * shared library is replaced on upgrade while `ALTER EXTENSION pg_net
   * UPDATE` might not have been executed yet), so fall back to a query that
   * doesn't reference the columns when they don't exist.
   */
  bool has_callbacks = get_attnum(queue_oid, "use_callbacks") != InvalidAttrNumber;

  SPIPlanPtr *plan = has_callbacks ? &del_return_queue_plan : &del_return_queue_plan_legacy;

  if (*plan == NULL) {
    const char *query = has_callbacks ? "\
        WITH\
        rows AS (\
          SELECT id\
          FROM net.http_request_queue\
          ORDER BY id\
          LIMIT $1\
        )\
        DELETE FROM net.http_request_queue q\
        USING rows WHERE q.id = rows.id\
        RETURNING q.id, q.method, q.url, timeout_milliseconds, array(select key || ': ' || value from jsonb_each_text(q.headers)), q.body, q.use_callbacks, q.on_success, q.on_error, q.calling_role"
                                      : "\
        WITH\
        rows AS (\
          SELECT id\
          FROM net.http_request_queue\
          ORDER BY id\
          LIMIT $1\
        )\
        DELETE FROM net.http_request_queue q\
        USING rows WHERE q.id = rows.id\
        RETURNING q.id, q.method, q.url, timeout_milliseconds, array(select key || ': ' || value from jsonb_each_text(q.headers)), q.body, false, null::text, null::text, null::text";

    SPIPlanPtr tmp = SPI_prepare(query, 1, (Oid[]){INT4OID});

    if (tmp == NULL)
      ereport(ERROR, errmsg("SPI_prepare failed: %s", SPI_result_code_string(SPI_result)));

    *plan = SPI_saveplan(tmp);
    if (*plan == NULL) ereport(ERROR, errmsg("SPI_saveplan failed"));
  }

  int ret_code = SPI_execute_plan(*plan, (Datum[]){Int32GetDatum(batch_size)}, NULL, false, 0);

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

  bool use_callbacks = DatumGetBool(SPI_getbinval(spi_tupval, spi_tupdesc, 7, &tupIsNull));
  EREPORT_NULL_ATTR(tupIsNull, use_callbacks);

  NullableDatum onSuccessBin = {.value  = SPI_getbinval(spi_tupval, spi_tupdesc, 8, &tupIsNull),
                                .isnull = tupIsNull};

  NullableDatum onErrorBin = {.value  = SPI_getbinval(spi_tupval, spi_tupdesc, 9, &tupIsNull),
                              .isnull = tupIsNull};

  NullableDatum callingRoleBin = {.value  = SPI_getbinval(spi_tupval, spi_tupdesc, 10, &tupIsNull),
                                  .isnull = tupIsNull};

  return (RequestQueueRow){id,         method,        url,           timeout_milliseconds,
                           headersBin, bodyBin,       use_callbacks, onSuccessBin,
                           onErrorBin, callingRoleBin};
}

static Jsonb *jsonb_headers_from_curl_handle(CURL *ez_handle) {
  struct curl_header *header, *prev = NULL;
  PG_JSONB_INIT_STATE(headers);
  (void)PG_JSONB_PUSH(headers, WJB_BEGIN_OBJECT, NULL);

  while ((header = curl_easy_nextheader(ez_handle, CURLH_HEADER, 0, prev))) {
    JsonbValue key   = {.type = jbvString,
                        .val  = {.string = {.val = header->name, .len = strlen(header->name)}}};
    JsonbValue value = {.type = jbvString,
                        .val  = {.string = {.val = header->value, .len = strlen(header->value)}}};
    (void)PG_JSONB_PUSH(headers, WJB_KEY, &key);
    (void)PG_JSONB_PUSH(headers, WJB_VALUE, &value);
    prev = header;
  }

  return PG_JSONB_OBJECT_FINISH(headers);
}

/*
 * Execute a callback command with SPI inside a subtransaction, as the role
 * that enqueued the request. A failing callback is reported as a WARNING and
 * doesn't abort the batch: the request is considered processed.
 */
static void exec_callback(CurlHandle *handle, const char *label, const char *command, int nargs,
                          Oid *argtypes, Datum *values, const char *nulls) {
  Oid roleid = handle->calling_role ? get_role_oid(handle->calling_role, true) : InvalidOid;

  if (!OidIsValid(roleid)) {
    ereport(WARNING, errmsg("pg_net: skipping %s callback of request id " INT64_FORMAT
                            ": role \"%s\" does not exist",
                            label, handle->id, handle->calling_role ? handle->calling_role : ""));
    return;
  }

  Oid saved_userid;
  int saved_sec_context;
  GetUserIdAndSecContext(&saved_userid, &saved_sec_context);

  MemoryContext oldcontext = CurrentMemoryContext;
  ResourceOwner oldowner   = CurrentResourceOwner;

  BeginInternalSubTransaction(NULL);

  // run the callback as the enqueuing role, and prevent it from escalating
  // via SET ROLE/SET SESSION AUTHORIZATION
  SetUserIdAndSecContext(roleid, saved_sec_context | SECURITY_LOCAL_USERID_CHANGE |
                                     SECURITY_RESTRICTED_OPERATION);

  PG_TRY();
  {
    int rc = SPI_execute_with_args(command, nargs, argtypes, values, nulls, false, 0);
    if (rc < 0)
      ereport(ERROR, errmsg("SPI_execute_with_args failed: %s", SPI_result_code_string(rc)));

    SetUserIdAndSecContext(saved_userid, saved_sec_context);
    ReleaseCurrentSubTransaction();
    MemoryContextSwitchTo(oldcontext);
    CurrentResourceOwner = oldowner;
  }
  PG_CATCH();
  {
    SetUserIdAndSecContext(saved_userid, saved_sec_context);

    MemoryContextSwitchTo(oldcontext);
    ErrorData *edata = CopyErrorData();
    FlushErrorState();

    RollbackAndReleaseCurrentSubTransaction();
    MemoryContextSwitchTo(oldcontext);
    CurrentResourceOwner = oldowner;

    ereport(WARNING, errmsg("pg_net: %s callback of request id " INT64_FORMAT " failed: %s", label,
                            handle->id, edata->message));
    FreeErrorData(edata);
  }
  PG_END_TRY();
}

static void exec_request_callbacks(CurlHandle *handle, CURLcode curl_return_code) {
  if (curl_return_code == CURLE_OK) {
    if (handle->on_success == NULL) return; // fire-and-forget, discard the response

    long res_http_status_code = 0;
    EREPORT_CURL_GETINFO(handle->ez_handle, CURLINFO_RESPONSE_CODE, &res_http_status_code);

    Jsonb *jsonb_headers = jsonb_headers_from_curl_handle(handle->ez_handle);

    bool body_is_empty = !handle->body || handle->body->data[0] == '\0';

    Datum values[3] = {Int32GetDatum((int32)res_http_status_code), JsonbPGetDatum(jsonb_headers),
                       body_is_empty ? (Datum)0 : CStringGetTextDatum(handle->body->data)};

    exec_callback(handle, "on_success", handle->on_success, 3, (Oid[]){INT4OID, JSONBOID, TEXTOID},
                  values, body_is_empty ? "  n" : "   ");
  } else {
    bool timed_out = curl_return_code == CURLE_OPERATION_TIMEDOUT;

    curl_timeout_msg timeout_msg = {.msg = ""};
    if (timed_out)
      timeout_msg = detailed_timeout_strerror(handle->ez_handle, handle->timeout_milliseconds);

    const char *error_msg = timed_out ? timeout_msg.msg : curl_easy_strerror(curl_return_code);

    if (handle->on_error == NULL) {
      ereport(LOG, errmsg("pg_net: request id " INT64_FORMAT " failed: %s", handle->id, error_msg));
      return;
    }

    Datum values[2] = {CStringGetTextDatum(error_msg), BoolGetDatum(timed_out)};

    exec_callback(handle, "on_error", handle->on_error, 2, (Oid[]){TEXTOID, BOOLOID}, values, "  ");
  }
}

void insert_response(CurlHandle *handle, CURLcode curl_return_code) {
  // requests made through net.http_request() are handled by callbacks and
  // never touch the net._http_response table
  if (handle->use_callbacks) {
    exec_request_callbacks(handle, curl_return_code);
    return;
  }

  enum { nparams = 7 }; // using an enum because const size_t nparams doesn't compile
  Datum vals[nparams];
  char  nulls[nparams];
  MemSet(nulls, 'n', nparams);

  vals[0]  = Int64GetDatum(handle->id);
  nulls[0] = ' ';

  if (curl_return_code == CURLE_OK) {
    Jsonb *jsonb_headers        = jsonb_headers_from_curl_handle(handle->ez_handle);
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

    struct curl_header *hdr;
    if (curl_easy_header(handle->ez_handle, "content-type", 0, CURLH_HEADER, -1, &hdr) ==
        CURLHE_OK) {
      vals[4]  = CStringGetTextDatum(hdr->value);
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

  if (ins_response_plan == NULL) {
    SPIPlanPtr tmp = SPI_prepare(
        "\
        insert into net._http_response(id, status_code, content, headers, content_type, timed_out, error_msg) values ($1, $2, $3, $4, $5, $6, $7)",
        nparams, (Oid[nparams]){INT8OID, INT4OID, TEXTOID, JSONBOID, TEXTOID, BOOLOID, TEXTOID});

    if (tmp == NULL)
      ereport(ERROR, errmsg("SPI_prepare failed: %s", SPI_result_code_string(SPI_result)));

    ins_response_plan = SPI_saveplan(tmp);
    if (ins_response_plan == NULL) ereport(ERROR, errmsg("SPI_saveplan failed"));

    SPI_freeplan(tmp);
  }

  int ret_code = SPI_execute_plan(ins_response_plan, vals, nulls, false, 0);

  if (ret_code != SPI_OK_INSERT) {
    ereport(ERROR, errmsg("Error when inserting response: %s", SPI_result_code_string(ret_code)));
  }
}

void pfree_handle(CurlHandle *handle) {
  pfree(handle->url);
  pfree(handle->method);
  if (handle->req_body) pfree(handle->req_body);
  if (handle->on_success) pfree(handle->on_success);
  if (handle->on_error) pfree(handle->on_error);
  if (handle->calling_role) pfree(handle->calling_role);

  if (handle->body) destroyStringInfo(handle->body);

  if (handle->request_headers) // curl_slist_free_all already handles the NULL
                               // case, but be explicit about it
    curl_slist_free_all(handle->request_headers);
}
