#ifndef UTIL_H
#define UTIL_H

#include <utils/jsonb.h>

extern struct curl_slist *pg_text_array_to_slist(ArrayType *array,
                                                 struct curl_slist *headers);
extern void parseHeaders(char *contents, JsonbParseState *headers);

#endif
