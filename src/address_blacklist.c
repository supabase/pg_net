#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "pg_prelude.h"
#include "curl_prelude.h"
#include "address_blacklist.h"

#ifndef TRUE
#define TRUE 1
#endif
 
#ifndef FALSE
#define FALSE 0
#endif


struct ip {
  /* The user-provided IP address or network (use CIDR) to filter */
  char *str;
  /* IP address family AF_INET (IPv4) or AF_INET6 (IPv6) */
  int family;
  /* IP in network byte format */
  union netaddr {
    struct in_addr ipv4;
#ifdef AF_INET6
    struct in6_addr ipv6;
#endif
  } netaddr;
  /* IP bits to match against.
   * This is equal to the CIDR notation or max bits if no CIDR.
   * For example if ip->str is 127.0.0.0/8 then ip->maskbits is 8.
   */
  int maskbits;
  struct ip *next;
};


struct ip *ip_list_append(struct ip *list, const char *data)
{
  struct ip *ip, *last;
  char *cidr;
 
  ip = calloc(1, sizeof(*ip));
  if(!ip)
    return NULL;
 
  if(strchr(data, ':')) {
#ifdef AF_INET6
    ip->family = AF_INET6;
#else
    free(ip);
    return NULL;
#endif
  }
  else
    ip->family = AF_INET;
 
  ip->str = strdup(data);
  if(!ip->str) {
    free(ip);
    return NULL;
  }
 
  /* determine the number of bits that this IP matches against */
  cidr = strchr(ip->str, '/');
  if(cidr) {
    ip->maskbits = atoi(cidr + 1);
    if(ip->maskbits <= 0 ||
#ifdef AF_INET6
       (ip->family == AF_INET6 && ip->maskbits > 128) ||
#endif
       (ip->family == AF_INET && ip->maskbits > 32)) {
      free(ip->str);
      free(ip);
      return NULL;
    }
    /* ignore the CIDR notation when converting ip->str to ip->netaddr */
    *cidr = '\0';
  }
  else if(ip->family == AF_INET)
    ip->maskbits = 32;
#ifdef AF_INET6
  else if(ip->family == AF_INET6)
    ip->maskbits = 128;
#endif
 
  /* !checksrc! disable BANNEDFUNC 1 */
  if(inet_pton(ip->family, ip->str, &ip->netaddr) != 1) {
    free(ip->str);
    free(ip);
    return NULL;
  }
 
  if(cidr)
    *cidr = '/';
 
  if(!list)
    return ip;
  for(last = list; last->next; last = last->next)
    ;
  last->next = ip;
  return list;
}

static int ip_match(struct ip *ip, void *netaddr)
{
  int bytes, tailbits;
  const unsigned char *x, *y;
 
  x = (const unsigned char *)&ip->netaddr;
  y = (const unsigned char *)netaddr;
 
  for(bytes = ip->maskbits / 8; bytes; --bytes) {
    if(*x++ != *y++)
      return FALSE;
  }
 
  tailbits = ip->maskbits % 8;
  if(tailbits) {
    unsigned char tailmask = (unsigned char)((0xFF << (8 - tailbits)) & 0xFF);
    if((*x & tailmask) != (*y & tailmask))
      return FALSE;
  }
 
  return TRUE;
}
 
static int is_ipv4_mapped_ipv6_address(int family, void *netaddr)
{
  if(family == AF_INET6) {
    int i;
    const unsigned char *x = (const unsigned char *)netaddr;
    for(i = 0; i < 12; ++i) {
      if(x[i])
        break;
    }
    /* support formats ::x.x.x.x (deprecated) and ::ffff:x.x.x.x */
    if((i == 12 && (x[i] || x[i + 1] || x[i + 2] || x[i + 3])) ||
       (i == 10 && (x[i] == 0xFF && x[i + 1] == 0xFF)))
      return TRUE;
  }
 
  return FALSE;
}

// static void ip_list_free_all(struct ip *list)
// {
//   struct ip *next;
//   while(list) {
//     next = list->next;
//     free(list->str);
//     free(list);
//     list = next;
//   }
// }

// static void free_connection_filter(struct connection_filter *filter)
// {
//   if(filter) {
//     ip_list_free_all(filter->list);
//     free(filter);
//   }
// }

curl_socket_t opensocket(void *clientp, curlsocktype purpose,
                                struct curl_sockaddr *address)
{
  /* filter the address */
  if(purpose == CURLSOCKTYPE_IPCXN) {
    void *cinaddr = NULL;
 
    if(address->family == AF_INET)
      cinaddr = &((struct sockaddr_in *)(void *)&address->addr)->sin_addr;
#ifdef AF_INET6
    else if(address->family == AF_INET6)
      cinaddr = &((struct sockaddr_in6 *)(void *)&address->addr)->sin6_addr;
#endif
 
    if(cinaddr) {
      struct ip *ip;
      struct connection_filter *filter = (struct connection_filter *)clientp;
#ifdef AF_INET6
      int mapped = !filter->ipv6_v6only &&
                   is_ipv4_mapped_ipv6_address(address->family, cinaddr);
#endif
 
      for(ip = filter->list; ip; ip = ip->next) {
        if(ip->family == address->family && ip_match(ip, cinaddr))
          break;
#ifdef AF_INET6
        if(mapped && ip->family == AF_INET && address->family == AF_INET6 &&
           ip_match(ip, (unsigned char *)cinaddr + 12))
          break;
#endif
      }
 
      if(ip && filter->type == CONNECTION_FILTER_BLACKLIST) {
        if(filter->verbose) {
          char buf[128] = { 0 };
          /* !checksrc! disable BANNEDFUNC 1 */
          inet_ntop(address->family, cinaddr, buf, sizeof(buf));
          fprintf(stderr, "* Rejecting IP %s due to blacklist entry %s.\n",
                  buf, ip->str);
        }
        return CURL_SOCKET_BAD;
      }
      else if(!ip && filter->type == CONNECTION_FILTER_WHITELIST) {
        if(filter->verbose) {
          char buf[128] = { 0 };
          /* !checksrc! disable BANNEDFUNC 1 */
          inet_ntop(address->family, cinaddr, buf, sizeof(buf));
          fprintf(stderr,
                  "* Rejecting IP %s due to missing whitelist entry.\n", buf);
        }
        return CURL_SOCKET_BAD;
      }
    }
  }
 
  return socket(address->family, address->socktype, address->protocol);
}
