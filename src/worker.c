#include <postgres.h>

#include <access/xact.h>
#include <catalog/pg_type.h>
#include <commands/extension.h>
#include <executor/spi.h>
#include <miscadmin.h>
#include <pgstat.h>
#include <postmaster/bgworker.h>
#include <storage/ipc.h>
#include <storage/proc.h>
#include <utils/builtins.h>
#include <utils/guc.h>
#include <utils/jsonb.h>
#include <utils/memutils.h>
#include <utils/snapmgr.h>

#include <curl/curl.h>

#include <uv.h>

#include "util.h"

PG_MODULE_MAGIC;

void _PG_init(void);
void worker_main(Datum main_arg) pg_attribute_noreturn();

static char *ttl;

static volatile sig_atomic_t got_sighup = false;

static uv_loop_t *loop;
static uv_idle_t idle;
static uv_timer_t timer;
static CURLM *cm;

static void handle_sigterm(SIGNAL_ARGS) { uv_stop(loop); }

static void handle_sighup(SIGNAL_ARGS) {
    int save_errno = errno;
    got_sighup = true;
    if (MyProc)
        SetLatch(&MyProc->procLatch);
    errno = save_errno;
}

struct curl_data {
    bool done;
    int64 id;
    char *method;
    char *url;
    struct curl_slist *request_headers;
    char *request_body;
    JsonbParseState *response_headers;
    StringInfo response_body;
    MemoryContext mem_ctx;
};

struct curl_context {
    uv_poll_t poll;
    curl_socket_t socket_fd;
    struct curl_data *data;
};

static bool is_extension_loaded(void) {
    Oid extension_oid;

    StartTransactionCommand();
    extension_oid = get_extension_oid("pg_net", true);
    CommitTransactionCommand();

    return OidIsValid(extension_oid);
}

static void close_cb(uv_handle_t *handle) {
    struct curl_context *ctx =
        (struct curl_context *)uv_handle_get_data(handle);

    /* elog(LOG, "curl_close_cb %ld", ctx->data->id); */

    if (ctx->data->done) {
        MemoryContext mem_ctx = MemoryContextSwitchTo(ctx->data->mem_ctx);
        if (ctx->data->method) {
            free(ctx->data->method);
        }
        if (ctx->data->url) {
            free(ctx->data->url);
        }
        if (ctx->data->request_headers) {
            curl_slist_free_all(ctx->data->request_headers);
        }
        if (ctx->data->request_body) {
            free(ctx->data->request_body);
        }
        // response_body &response_headers should be freed when mem_ctx is
        // freed.
        MemoryContextSwitchTo(mem_ctx);
        MemoryContextDelete(ctx->data->mem_ctx);
        free(ctx->data);
    }

    free(ctx);
}

static size_t body_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    struct curl_data *data = (struct curl_data *)userp;
    MemoryContext mem_ctx = MemoryContextSwitchTo(data->mem_ctx);
    size_t realsize = size * nmemb;
    StringInfo si = data->response_body;

    /* elog(LOG, "body_cb"); */

    appendBinaryStringInfo(si, (const char *)contents, (int)realsize);

    MemoryContextSwitchTo(mem_ctx);
    return realsize;
}

static size_t header_cb(void *contents, size_t size, size_t nmemb,
                        void *userp) {
    struct curl_data *data = (struct curl_data *)userp;
    MemoryContext mem_ctx = MemoryContextSwitchTo(data->mem_ctx);
    size_t realsize = size * nmemb;
    JsonbParseState *headers = data->response_headers;

    /* per curl docs, the status code is included in the header data
     * (it starts with: HTTP/1.1 200 OK or HTTP/2 200 OK)*/
    bool firstLine = strncmp(contents, "HTTP/", 5) == 0;
    /* the final(end of headers) last line is empty - just a CRLF */
    bool lastLine = strncmp(contents, "\r\n", 2) == 0;

    /* elog(LOG, "header_cb"); */

    /*Ignore non-header data in the first header line and last header
line*/
    if (!firstLine && !lastLine) {
        parseHeaders(contents, headers);
    }

    MemoryContextSwitchTo(mem_ctx);
    return realsize;
}

static void submit_request(int64 id, char *method, char *url, int32 timeout_milliseconds,
                           struct curl_slist *headers, char *body) {
    struct curl_data *data = (struct curl_data *)malloc(sizeof(*data));
    MemoryContext mem_ctx =
        AllocSetContextCreate(TopMemoryContext, NULL, ALLOCSET_DEFAULT_SIZES);
    CURL *easy = curl_easy_init();

    /* elog(LOG, "submit_request"); */

    headers = curl_slist_append(headers, "User-Agent: pg_net/0.2");

    data->mem_ctx = mem_ctx;
    mem_ctx = MemoryContextSwitchTo(data->mem_ctx);
    data->done = false;
    data->id = id;
    data->method = method;
    data->url = url;
    data->request_headers = headers;
    data->request_body = body;
    data->response_headers = NULL;
    data->response_body = makeStringInfo();

    pushJsonbValue(&data->response_headers, WJB_BEGIN_OBJECT, NULL);

    if (strcasecmp(method, "GET") == 0) {
        if (body) {
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, "GET");
        }
    } else if (strcasecmp(method, "POST") == 0) {
        if (body) {
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body);
        }
    } else {
        elog(ERROR, "error: Unsupported request method %s\n", method);
    }

    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, body_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, data);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, data);
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, data->request_headers);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, timeout_milliseconds);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, (void *)data);
    if (log_min_messages <= DEBUG2) {
        curl_easy_setopt(easy, CURLOPT_VERBOSE, 1);
    }
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_multi_add_handle(cm, easy);

    MemoryContextSwitchTo(mem_ctx);
}

static void check_curl_multi_info(void) {
    struct curl_data *data;
    CURL *easy;
    CURLMsg *msg;
    int pending;
    int rc;

    /* elog(LOG, "check_curl_multi_info"); */

    while ((msg = curl_multi_info_read(cm, &pending))) {
        switch (msg->msg) {
        case CURLMSG_DONE:
            easy = msg->easy_handle;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &data);
            data->done = true;

            /* elog(LOG, "CURLMSG_DONE: %ld", data->id); */

            StartTransactionCommand();
            PushActiveSnapshot(GetTransactionSnapshot());
            SPI_connect();
            rc = msg->data.result;
            if (rc != CURLE_OK) {
                char *sql = "INSERT INTO\n"
                            "  net._http_response(id, error_msg)\n"
                            "VALUES ($1, $2)";

                int nargs = 2;
                Oid argtypes[2];
                Datum values[2];
                const char *error_msg;

                argtypes[0] = INT8OID;
                values[0] = Int64GetDatum(data->id);

                error_msg = curl_easy_strerror(rc);
                argtypes[1] = CSTRINGOID;
                values[1] = CStringGetDatum(error_msg);

                if (SPI_execute_with_args(sql, nargs, argtypes, values, NULL,
                                          false, 0) != SPI_OK_INSERT) {
                    elog(ERROR, "SPI_exec failed:\n%s", sql);
                }
            } else {
                char *sql = "INSERT INTO\n"
                            "  net._http_response(\n"
                            "    id,\n"
                            "    status_code,\n"
                            "    content,\n"
                            "    headers,\n"
                            "    content_type,\n"
                            "    timed_out\n"
                            "  )\n"
                            "VALUES ($1, $2, $3, $4, $5, $6)";

                int nargs = 6;
                Oid argtypes[6];
                Datum values[6];
                char nulls[6];
                int status_code;
                char *content_type = NULL;
                bool timed_out = false;

                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status_code);
                curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &content_type);
                // NOTE: ctx mysteriously becomes NULL after the getinfo calls
                // above.
                curl_easy_getinfo(easy, CURLINFO_PRIVATE, &data);

                argtypes[0] = INT8OID;
                values[0] = Int64GetDatum(data->id);
                nulls[0] = ' ';

                argtypes[1] = INT4OID;
                values[1] = Int32GetDatum(status_code);
                nulls[1] = ' ';

                argtypes[2] = CSTRINGOID;
                values[2] = CStringGetDatum(data->response_body->data);
                if (data->response_body->data[0] == '\0') {
                    nulls[2] = 'n';
                } else {
                    nulls[2] = ' ';
                }

                argtypes[3] = JSONBOID;
                values[3] = JsonbPGetDatum(JsonbValueToJsonb(pushJsonbValue(
                    &data->response_headers, WJB_END_OBJECT, NULL)));
                nulls[3] = ' ';

                argtypes[4] = CSTRINGOID;
                values[4] = CStringGetDatum(content_type);
                if (!content_type) {
                    nulls[4] = 'n';
                } else {
                    nulls[4] = ' ';
                }

                argtypes[5] = BOOLOID;
                values[5] = BoolGetDatum(timed_out);
                nulls[5] = ' ';

                if (SPI_execute_with_args(sql, nargs, argtypes, values, nulls,
                                          false, 0) != SPI_OK_INSERT) {
                    elog(ERROR, "SPI_exec failed:\n%s", sql);
                }
            }
            SPI_finish();
            PopActiveSnapshot();
            CommitTransactionCommand();

            curl_multi_remove_handle(cm, easy);
            curl_easy_cleanup(easy);
            break;
        default:
            elog(ERROR, "Unexpected CURLMSG: %d", msg->msg);
        }
    }
}

static void poll_cb(uv_poll_t *poll, int status, int events) {
    int flags = 0;
    struct curl_context *ctx;
    int running_handles;

    /* elog(LOG, "poll_cb: req %p, status %d, events %d", poll, status, events);
     */

    if (status < 0)
        flags = CURL_CSELECT_ERR;
    if (!status && events & UV_READABLE)
        flags |= CURL_CSELECT_IN;
    if (!status && events & UV_WRITABLE)
        flags |= CURL_CSELECT_OUT;

    ctx = (struct curl_context *)uv_handle_get_data((uv_handle_t *)poll);

    curl_multi_socket_action(cm, ctx->socket_fd, flags, &running_handles);

    check_curl_multi_info();
}

static int socket_cb(CURL *easy, curl_socket_t s, int what, void *userp,
                     void *socketp) {
    struct curl_context *ctx;
    struct curl_data *data;
    int events = 0;

    curl_easy_getinfo(easy, CURLINFO_PRIVATE, &data);

    /* elog(LOG, "socket_cb: socket %d, action %d, socketp %p", s, what,
     * socketp); */

    switch (what) {
    case CURL_POLL_IN:
    case CURL_POLL_OUT:
    case CURL_POLL_INOUT:
        if (socketp) {
            ctx = (struct curl_context *)socketp;
        } else {
            /* fprintf(stderr, "(%d) 1st socket_cb\n", data->id); */

            ctx = (struct curl_context *)malloc(sizeof(*ctx));
            ctx->socket_fd = s;
            ctx->data = data;

            uv_poll_init_socket(loop, &ctx->poll, s);
            uv_handle_set_data((uv_handle_t *)&ctx->poll, (void *)ctx);

            curl_multi_assign(cm, s, (void *)ctx);
        }

        if (what != CURL_POLL_IN)
            events |= UV_WRITABLE;
        if (what != CURL_POLL_OUT)
            events |= UV_READABLE;

        uv_poll_start(&ctx->poll, events, poll_cb);
        break;
    case CURL_POLL_REMOVE:
        /* fprintf(stderr, "(%d) CURL_POLL_REMOVE\n", data->id); */

        if (socketp) {
            ctx = (struct curl_context *)socketp;

            uv_poll_stop(&ctx->poll);
            uv_close((uv_handle_t *)&ctx->poll, close_cb);

            curl_multi_assign(cm, s, NULL);
        } else {
            elog(ERROR, "Missing socketp in CURL_POLL_REMOVE");
        }
        break;
    default:
        elog(ERROR, "Unexpected CURL_POLL symbol: %d\n", what);
    }

    return 0;
}

static void on_timeout(uv_timer_t *req) {
    int running_handles;
    curl_multi_socket_action(cm, CURL_SOCKET_TIMEOUT, 0, &running_handles);
    check_curl_multi_info();
}

static int timer_cb(CURLM *cm, long timeout_ms, void *userp) {
    if (timeout_ms < 0) {
        uv_timer_stop(&timer);
    } else {
        // NOTE: 0 means directly call socket_action, but we'll do it in a bit
        if (timeout_ms == 0) {
            timeout_ms = 1;
        }
        uv_timer_start(&timer, on_timeout, timeout_ms, 0);
    }

    return 0;
}

static void idle_cb(uv_idle_t *idle) {
    WaitLatch(&MyProc->procLatch,
              WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH, 1000,
              PG_WAIT_EXTENSION);
    ResetLatch(&MyProc->procLatch);

    if (!is_extension_loaded()) {
        elog(DEBUG2, "idle_cb: extension not yet loaded");
        return;
    }

    if (got_sighup) {
        got_sighup = false;
        ProcessConfigFile(PGC_SIGHUP);
    }

    // NOTE: Any (p)allocations we do within a transaction are freed
    // automatically because it's within a memory context that is local to the
    // transaction.
    // https://github.com/postgres/postgres/tree/master/src/backend/utils/mmgr
    StartTransactionCommand();
    PushActiveSnapshot(GetTransactionSnapshot());
    SPI_connect();
    {
        char *sql = "DELETE FROM\n"
                    "  net._http_response\n"
                    "WHERE\n"
                    "  clock_timestamp() - created > $1::interval";
        int nargs = 1;
        Oid argtypes[1];
        Datum values[1];
        int rc;

        argtypes[0] = TEXTOID;
        values[0] = CStringGetTextDatum(ttl);

        rc =
            SPI_execute_with_args(sql, nargs, argtypes, values, NULL, false, 0);
        if (rc != SPI_OK_DELETE) {
            elog(ERROR, "SPI_exec failed with error code %d:\n%s", rc, sql);
        }
    }
    {
        char *sql =
            "DELETE FROM\n"
            "  net.http_request_queue\n"
            "RETURNING\n"
            "  id,\n"
            "  method,\n"
            "  url,\n"
						"  timeout_milliseconds,\n"
            "  array(\n"
            "    select key || ': ' || value from jsonb_each_text(headers)\n"
            "  ),\n"
            "  body";

        int rc = SPI_execute(sql, false, 0);
        if (rc != SPI_OK_DELETE_RETURNING) {
            elog(ERROR, "SPI_execute() failed with error code %d:\n%s", rc,
                 sql);
        }

        for (uint64 i = 0; i < SPI_processed; i++) {
            bool isnull;
            Datum tmp;

            int64 id = DatumGetInt64(SPI_getbinval(
                SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 1, &isnull));

            char *method_tmp = TextDatumGetCString(SPI_getbinval(
                SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 2, &isnull));
            char *method = (char *)malloc(strlen(method_tmp) + 1);

            char *url_tmp = TextDatumGetCString(SPI_getbinval(
                SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 3, &isnull));
            char *url = (char *)malloc(strlen(url_tmp) + 1);

            int32 timeout_milliseconds = DatumGetInt32(SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 4, &isnull));

            struct curl_slist *headers = NULL;

            char *body = NULL;

            memcpy(method, method_tmp, strlen(method_tmp) + 1);
            memcpy(url, url_tmp, strlen(url_tmp) + 1);
            tmp = SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 5,
                                &isnull);
            if (!isnull) {
                headers =
                    pg_text_array_to_slist(DatumGetArrayTypeP(tmp), headers);
            }
            tmp = SPI_getbinval(SPI_tuptable->vals[i], SPI_tuptable->tupdesc, 6,
                                &isnull);
            if (!isnull) {
                char *body_tmp = TextDatumGetCString(tmp);
                body = (char *)malloc(strlen(body_tmp) + 1);
                memcpy(body, body_tmp, strlen(body_tmp) + 1);
            }

            submit_request(id, method, url, timeout_milliseconds, headers, body);
            /* elog(LOG, "submitted"); */
        }
    }
    SPI_finish();
    PopActiveSnapshot();
    CommitTransactionCommand();
}

void worker_main(Datum main_arg) {
    int rc;

    pqsignal(SIGTERM, handle_sigterm);
    pqsignal(SIGHUP, handle_sighup);
    BackgroundWorkerUnblockSignals();
    // TODO: Unhardcode dbname
    BackgroundWorkerInitializeConnection("postgres", NULL, 0);

    loop = uv_default_loop();

    uv_idle_init(loop, &idle);
    uv_idle_start(&idle, idle_cb);

    rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc) {
        elog(ERROR, "curl_global_init() error: %s", curl_easy_strerror(rc));
    }

    uv_timer_init(loop, &timer);

    cm = curl_multi_init();
    curl_multi_setopt(cm, CURLMOPT_MAX_TOTAL_CONNECTIONS, 100);
    curl_multi_setopt(cm, CURLMOPT_SOCKETFUNCTION, socket_cb);
    curl_multi_setopt(cm, CURLMOPT_TIMERFUNCTION, timer_cb);

    uv_run(loop, UV_RUN_DEFAULT);

    proc_exit(0);
}

void _PG_init(void) {
    BackgroundWorker bgw;

    if (IsBinaryUpgrade) {
        return;
    }

    if (!process_shared_preload_libraries_in_progress) {
        ereport(ERROR, errmsg("pg_net is not in shared_preload_libraries"),
                errhint("Add pg_net to the shared_preload_libraries "
                        "configuration variable in postgresql.conf."));
    }

    DefineCustomStringVariable("pg_net.ttl",
                               "time to live for request/response rows",
                               "should be a valid interval type", &ttl,
                               "3 days", PGC_SIGHUP, 0, NULL, NULL, NULL);

    memset(&bgw, 0, sizeof(bgw));
    bgw.bgw_flags =
        BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
    bgw.bgw_start_time = BgWorkerStart_RecoveryFinished;
    snprintf(bgw.bgw_library_name, BGW_MAXLEN, "pg_net");
    snprintf(bgw.bgw_function_name, BGW_MAXLEN, "worker_main");
    snprintf(bgw.bgw_name, BGW_MAXLEN, "pg_net worker");
    bgw.bgw_restart_time = 10;
    RegisterBackgroundWorker(&bgw);
}
