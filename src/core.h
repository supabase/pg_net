#ifndef CORE_H
#define CORE_H

typedef enum {
  WS_NOT_YET = 1,
  WS_RUNNING,
  WS_EXITED,
} WorkerStatus;

typedef struct {
  pg_atomic_uint32  got_restart;
  pg_atomic_uint32  should_wake;
  pg_atomic_uint32  status;
  Latch             latch;
  ConditionVariable cv;
  int               epfd;
  CURLM             *curl_mhandle;
} WorkerState;

uint64 delete_expired_responses(char *ttl, int batch_size);

uint64 consume_request_queue(CURLM *curl_mhandle, int batch_size, MemoryContext curl_memctx);

void insert_curl_responses(WorkerState *wstate, MemoryContext curl_memctx);

void set_curl_mhandle(WorkerState *wstate);

#endif
