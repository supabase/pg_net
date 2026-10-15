#ifndef ADDRESS_BLACKLIST_H
#define ADDRESS_BLACKLIST_H
enum connection_filter_t {
  CONNECTION_FILTER_BLACKLIST,
  CONNECTION_FILTER_WHITELIST
};
struct connection_filter {
  struct ip *list;
  enum connection_filter_t type;
  int verbose;
#ifdef AF_INET6
  /* If the address being filtered is an IPv4-mapped IPv6 address then it is
   * checked against IPv4 list entries as well, unless ipv6_v6only is set TRUE.
   */
  int ipv6_v6only;
#endif
};
curl_socket_t opensocket(void *clientp, curlsocktype purpose,
                                struct curl_sockaddr *address);
struct ip *ip_list_append(struct ip *list, const char *data);
#endif

