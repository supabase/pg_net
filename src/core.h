#ifndef CORE_H
#define CORE_H

typedef struct {
  int epfd;
  CURLM *curl_mhandle;
} LoopState;

uint64 delete_expired_responses(char *ttl, int batch_size);

uint64 consume_request_queue(CURLM *curl_mhandle, int batch_size, MemoryContext curl_memctx);

void insert_curl_responses(LoopState *lstate, MemoryContext curl_memctx);

void set_curl_mhandle(CURLM *curl_mhandle, LoopState *lstate);

#endif
