#ifndef EVENT_H
#define EVENT_H

#include <curl/multi.h>

#include <sys/epoll.h>

#include "core.h"

typedef struct epoll_event event;

int wait_event(int fd, event *events, size_t maxevents, int wait_milliseconds);
int event_monitor(void);
void ev_monitor_close(LoopState *lstate);
int multi_timer_cb(CURLM *multi, long timeout_ms, LoopState *lstate);
int multi_socket_cb(CURL *easy, curl_socket_t sockfd, int what, LoopState *lstate, void *socketp);
bool is_timer(event ev);
int get_curl_event(event ev);
int get_socket_fd(event ev);

#endif
