#ifndef EVENT_H
#define EVENT_H

#include <curl/multi.h>

#include "core.h"

#ifdef __linux__
#define WAIT_USE_EPOLL
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define WAIT_USE_KQUEUE
#else
#error "no event loop implementation available"
#endif

#ifdef WAIT_USE_EPOLL

#include <sys/epoll.h>
#include <sys/timerfd.h>
typedef struct epoll_event event;

#else

#include <sys/event.h>
typedef struct kevent event;

#endif

int wait_event(int fd, event *events, size_t maxevents, int wait_milliseconds);
int event_monitor(void);
void ev_monitor_close(LoopState *lstate);
int multi_timer_cb(CURLM *multi, long timeout_ms, LoopState *lstate);
int multi_socket_cb(CURL *easy, curl_socket_t sockfd, int what, LoopState *lstate, void *socketp);
bool is_timer(event ev);
int get_curl_event(event ev);
int get_socket_fd(event ev);

#endif
