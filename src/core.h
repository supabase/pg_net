#ifndef CORE_H
#define CORE_H

typedef struct itimerspec itimerspec;
typedef struct epoll_event epoll_event;

typedef struct {
  int epfd;
  int timerfd;
  CURLM *curl_mhandle;
} LoopState;

void delete_expired_responses(char *ttl, int batch_size);

void consume_request_queue(CURLM *curl_mhandle, int batch_size, MemoryContext curl_memctx);

void insert_curl_responses(LoopState *lstate, MemoryContext curl_memctx);

void set_curl_mhandle(CURLM *curl_mhandle, LoopState *lstate);

#endif
