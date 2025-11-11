#include "pg_prelude.h"

#include "curl_prelude.h"

#include "errors.h"
#include "util.h"

PG_FUNCTION_INFO_V1(_urlencode_string);
PG_FUNCTION_INFO_V1(_encode_url_with_params_array);

Datum _urlencode_string(PG_FUNCTION_ARGS) {
  if (PG_GETARG_POINTER(0) == NULL) PG_RETURN_NULL();

  char *str            = text_to_cstring(PG_GETARG_TEXT_P(0));
  char *urlencoded_str = NULL;

  urlencoded_str = curl_escape(str, strlen(str));

  pfree(str);

  PG_RETURN_TEXT_P(cstring_to_text(urlencoded_str));
}

Datum _encode_url_with_params_array(PG_FUNCTION_ARGS) {
  if (PG_GETARG_POINTER(0) == NULL || PG_GETARG_POINTER(1) == NULL) PG_RETURN_NULL();

  char      *url    = text_to_cstring(PG_GETARG_TEXT_P(0));
  ArrayType *params = PG_GETARG_ARRAYTYPE_P(1);

  char *full_url = NULL;

  ArrayIterator iterator;
  Datum         value;
  bool          isnull;
  char         *param;

  CURLU *h = curl_url();
  EREPORT_CURL_URL_SET(h, CURLUPART_URL, url, 0);

  iterator = array_create_iterator(params, 0, NULL);
  while (array_iterate(iterator, &value, &isnull)) {
    if (isnull) continue;

    param = TextDatumGetCString(value);
    EREPORT_CURL_URL_SET(h, CURLUPART_QUERY, param, CURLU_APPENDQUERY);
    pfree(param);
  }
  array_free_iterator(iterator);

  EREPORT_CURL_URL_GET(h, CURLUPART_URL, &full_url, 0, url);

  pfree(url);
  curl_url_cleanup(h);

  PG_RETURN_TEXT_P(cstring_to_text(full_url));
}
