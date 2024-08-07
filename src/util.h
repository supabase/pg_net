#ifndef UTIL_H
#define UTIL_H

#define CURL_EZ_SETOPT(hdl, opt, prm) \
  do { \
      if (curl_easy_setopt(hdl, opt, prm) != CURLE_OK) \
        ereport(ERROR, errmsg("Could not curl_easy_setopt(%s)", #opt)); \
  } while (0)

#define CURL_EZ_GETINFO(hdl, opt, prm) \
  do { \
      if (curl_easy_getinfo(hdl, opt, prm) != CURLE_OK) \
        ereport(ERROR, errmsg("Could not curl_easy_getinfo(%s)", #opt)); \
  } while (0)

#define EREPORT_NULL_ATTR(tupIsNull, attr) \
  do { \
    if (tupIsNull) \
      ereport(ERROR, errmsg("%s cannot be null", #attr)); \
  } while (0)


extern struct curl_slist *pg_text_array_to_slist(ArrayType *array,
                                                 struct curl_slist *headers);

#endif
