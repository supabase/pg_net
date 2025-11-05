#ifndef ERRORS_H
#define ERRORS_H

#define EREPORT_CURL_SETOPT(hdl, opt, prm)                                                         \
  do {                                                                                             \
    if (curl_easy_setopt(hdl, opt, prm) != CURLE_OK)                                               \
      ereport(ERROR, errmsg("Could not curl_easy_setopt(%s)", #opt));                              \
  } while (0)

#define EREPORT_CURL_GETINFO(hdl, opt, prm)                                                        \
  do {                                                                                             \
    if (curl_easy_getinfo(hdl, opt, prm) != CURLE_OK)                                              \
      ereport(ERROR, errmsg("Could not curl_easy_getinfo(%s)", #opt));                             \
  } while (0)

#define EREPORT_CURL_MULTI_SETOPT(hdl, opt, prm)                                                   \
  do {                                                                                             \
    if (curl_multi_setopt(hdl, opt, prm) != CURLM_OK)                                              \
      ereport(ERROR, errmsg("Could not curl_multi_setopt(%s)", #opt));                             \
  } while (0)

#define EREPORT_CURL_SLIST_APPEND(list, str)                                                       \
  do {                                                                                             \
    struct curl_slist *new_list = curl_slist_append(list, str);                                    \
    if (new_list == NULL) ereport(ERROR, errmsg("curl_slist_append returned NULL"));               \
    list = new_list;                                                                               \
  } while (0)

#define EREPORT_NULL_ATTR(tupIsNull, attr)                                                         \
  do {                                                                                             \
    if (tupIsNull) ereport(ERROR, errmsg("%s cannot be null", #attr));                             \
  } while (0)

#define EREPORT_MULTI(multi_call)                                                                  \
  do {                                                                                             \
    CURLMcode code = multi_call;                                                                   \
    if (code != CURLM_OK)                                                                          \
      ereport(ERROR, errmsg("%s failed with %s", #multi_call, curl_multi_strerror(code)));         \
  } while (0)

#define CURL_TIMEOUT_MSG_SIZE 256

typedef struct {
  char msg[CURL_TIMEOUT_MSG_SIZE];
} curl_timeout_msg;

curl_timeout_msg detailed_timeout_strerror(CURL *ez_handle, int32 timeout_milliseconds);

#endif
