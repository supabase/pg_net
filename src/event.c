#include <postgres.h>
#include <curl/multi.h>
#include <stdbool.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/timerfd.h>

#include "event.h"

static int timerfd = 0;
static bool timer_created = false;

typedef struct epoll_event epoll_event;
typedef struct itimerspec itimerspec;

int inline wait_event(int fd, event *events, size_t maxevents, int wait_milliseconds){
  return epoll_wait(fd, events, maxevents, /*timeout=*/1000);
}

int inline event_monitor(){
  return epoll_create1(0);
}

void ev_monitor_close(LoopState *lstate){
  close(lstate->epfd);
  close(timerfd);
}

int multi_timer_cb(CURLM *multi, long timeout_ms, LoopState *lstate) {
  elog(DEBUG2, "multi_timer_cb: Setting timeout to %ld ms\n", timeout_ms);

  if (!timer_created){
    timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd < 0) {
      ereport(ERROR, errmsg("Failed to create timerfd"));
    }
    timerfd_settime(timerfd, 0, &(itimerspec){}, NULL);
    epoll_ctl(lstate->epfd, EPOLL_CTL_ADD, timerfd, &(epoll_event){.events = EPOLLIN, .data.fd = timerfd});

    timer_created = true;
  }

  itimerspec its =
    timeout_ms > 0 ?
    // assign the timeout normally
    (itimerspec){
      .it_value.tv_sec = timeout_ms / 1000,
      .it_value.tv_nsec = (timeout_ms % 1000) * 1000 * 1000,
    }:
    timeout_ms == 0 ?
    /* libcurl wants us to timeout now, however setting both fields of
     * new_value.it_value to zero disarms the timer. The closest we can
     * do is to schedule the timer to fire in 1 ns. */
    (itimerspec){
      .it_value.tv_sec = 0,
      .it_value.tv_nsec = 1,
    }:
     // libcurl passes a -1 to indicate the timer should be deleted
    (itimerspec){};

  int no_flags = 0;
  if (timerfd_settime(timerfd, no_flags, &its, NULL) < 0) {
    ereport(ERROR, errmsg("timerfd_settime failed"));
  }

  return 0;
}

int multi_socket_cb(CURL *easy, curl_socket_t sockfd, int what, LoopState *lstate, void *socketp) {
  static char *whatstrs[] = { "NONE", "CURL_POLL_IN", "CURL_POLL_OUT", "CURL_POLL_INOUT", "CURL_POLL_REMOVE" };
  elog(DEBUG2, "multi_socket_cb: sockfd %d received %s", sockfd, whatstrs[what]);

  int epoll_op;
  if(!socketp){
    epoll_op = EPOLL_CTL_ADD;
    bool *socket_exists = palloc(sizeof(bool));
    curl_multi_assign(lstate->curl_mhandle, sockfd, socket_exists);
  } else if (what == CURL_POLL_REMOVE){
    epoll_op = EPOLL_CTL_DEL;
    pfree(socketp);
    curl_multi_assign(lstate->curl_mhandle, sockfd, NULL);
  } else {
    epoll_op = EPOLL_CTL_MOD;
  }

  epoll_event ev = {
    .data.fd = sockfd,
    .events =
      (what & CURL_POLL_IN) ?
      EPOLLIN:
      (what & CURL_POLL_OUT) ?
      EPOLLOUT:
      0, // no event is assigned since here we get CURL_POLL_REMOVE and the sockfd will be removed
  };

  // epoll_ctl will copy ev, so there's no need to do palloc for the epoll_event
  // https://github.com/torvalds/linux/blob/e32cde8d2bd7d251a8f9b434143977ddf13dcec6/fs/eventpoll.c#L2408-L2418
  if (epoll_ctl(lstate->epfd, epoll_op, sockfd, &ev) < 0) {
    int e = errno;
    static char *opstrs[] = { "NONE", "EPOLL_CTL_ADD", "EPOLL_CTL_DEL", "EPOLL_CTL_MOD" };
    ereport(ERROR, errmsg("epoll_ctl with %s failed when receiving %s for sockfd %d: %s", whatstrs[what], opstrs[epoll_op], sockfd, strerror(e)));
  }

  return 0;
}

bool is_timer(event ev){
  return ev.data.fd == timerfd;
}

int get_curl_event(event ev){
  int ev_bitmask =
    ev.events & EPOLLIN ? CURL_CSELECT_IN:
    ev.events & EPOLLOUT ? CURL_CSELECT_OUT:
    CURL_CSELECT_ERR;
  return ev_bitmask;
}

int get_socket_fd(event ev){
  return ev.data.fd;
}
