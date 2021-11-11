#include <postgres.h>

#include <tcop/utility.h>
#include <utils/builtins.h>

#include <curl/curl.h>

#include "util.h"

PG_FUNCTION_INFO_V1(_urlencode_string);
PG_FUNCTION_INFO_V1(_encode_url_with_params_array);

Datum _urlencode_string(PG_FUNCTION_ARGS) {
    char *str = text_to_cstring(PG_GETARG_TEXT_P(0));
    char *urlencoded_str = NULL;

    urlencoded_str = curl_escape(str, strlen(str));

    pfree(str);

    PG_RETURN_TEXT_P(cstring_to_text(urlencoded_str));
}

Datum _encode_url_with_params_array(PG_FUNCTION_ARGS) {
    CURLU *h = curl_url();
    CURLUcode rc;

    char *url = text_to_cstring(PG_GETARG_TEXT_P(0));
    ArrayType *params = PG_GETARG_ARRAYTYPE_P(1);
    char *full_url = NULL;

    ArrayIterator iterator;
    Datum value;
    bool isnull;
    char *param;

    rc = curl_url_set(h, CURLUPART_URL, url, 0);
    if (rc != CURLUE_OK) {
        // TODO: Use curl_url_strerror once released.
        elog(ERROR, "%s", curl_easy_strerror((CURLcode)rc));
    }

    iterator = array_create_iterator(params, 0, NULL);
    while (array_iterate(iterator, &value, &isnull)) {
        if (isnull)
            continue;

        param = TextDatumGetCString(value);
        rc = curl_url_set(h, CURLUPART_QUERY, param, CURLU_APPENDQUERY);
        if (rc != CURLUE_OK) {
            elog(ERROR, "curl_url returned: %d", rc);
        }
        pfree(param);
    }
    array_free_iterator(iterator);

    rc = curl_url_get(h, CURLUPART_URL, &full_url, 0);
    if (rc != CURLUE_OK) {
        elog(ERROR, "curl_url returned: %d", rc);
    }

    pfree(url);
    curl_url_cleanup(h);

    PG_RETURN_TEXT_P(cstring_to_text(full_url));
}

struct curl_slist *pg_text_array_to_slist(ArrayType *array,
                                                 struct curl_slist *headers) {
    ArrayIterator iterator;
    Datum value;
    bool isnull;
    char *header;

    iterator = array_create_iterator(array, 0, NULL);

    while (array_iterate(iterator, &value, &isnull)) {
        if (isnull) {
            continue;
        }

        header = TextDatumGetCString(value);
        headers = curl_slist_append(headers, header);
        pfree(header);
    }
    array_free_iterator(iterator);

    return headers;
}

/*TODO: make the parsing more robust, test with invalid headers*/
void parseHeaders(char *contents, JsonbParseState *headers){
    /* per curl docs, the status code is included in the header data
     * (it starts with: HTTP/1.1 200 OK or HTTP/2 200 OK)*/
    bool firstLine = strncmp(contents, "HTTP/", 5) == 0;
    /* the final(end of headers) last line is empty - just a CRLF */
    bool lastLine = strncmp(contents, "\r\n", 2) == 0;
    char *token;
    char *tmp = pstrdup(contents);
    JsonbValue key, val;

    if (!firstLine && !lastLine) {
        /*The header comes as "Header-Key: val", split by whitespace and
        ditch
         * the colon later*/
        token = strtok(tmp, " ");

        key.type = jbvString;
        key.val.string.val = token;
        /*strlen - 1 because we ditch the last char - the colon*/
        key.val.string.len = strlen(token) - 1;
        (void)pushJsonbValue(&headers, WJB_KEY, &key);

        /*Every header line ends with CRLF, split and remove it*/
        token = strtok(NULL, "\r\n");

        val.type = jbvString;
        val.val.string.val = token;
        val.val.string.len = strlen(token);
        (void)pushJsonbValue(&headers, WJB_VALUE, &val);
    }
}
