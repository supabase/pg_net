#include <stddef.h>
#include <errno.h>
#include <unistd.h>

#include "pg_prelude.h"
#include "event.h"

#ifdef WAIT_USE_EPOLL

static int timerfd = 0;
static bool timer_created = false;

typedef struct epoll_event epoll_event;
typedef struct itimerspec itimerspec;

inline int wait_event(int fd, event *events, size_t maxevents, int timeout_milliseconds){
  return epoll_wait(fd, events, maxevents, timeout_milliseconds);
}

inline int event_monitor(){
  return epoll_create1(0);
}

void ev_monitor_close(LoopState *lstate){
  close(lstate->epfd);
  close(timerfd);
}

int multi_timer_cb(__attribute__ ((unused)) CURLM *multi, long timeout_ms, LoopState *lstate) {
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

int multi_socket_cb(__attribute__ ((unused)) CURL *easy, curl_socket_t sockfd, int what, LoopState *lstate, void *socketp) {
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

#else

typedef struct {
  curl_socket_t sockfd;
  int action;
} SocketInfo ;

int inline wait_event(int fd, event *events, size_t maxevents, int timeout_milliseconds){
  return kevent(fd, NULL, 0, events, maxevents, &(struct timespec){.tv_sec = timeout_milliseconds/1000});
}

int inline event_monitor(){
  return kqueue();
}

void ev_monitor_close(LoopState *lstate){
  close(lstate->epfd);
}

int multi_timer_cb(__attribute__ ((unused)) CURLM *multi, long timeout_ms, LoopState *lstate) {
  elog(DEBUG2, "multi_timer_cb: Setting timeout to %ld ms\n", timeout_ms);
  event timer_event;
  int id = 1;

  if (timeout_ms > 0) {
    EV_SET(&timer_event, id, EVFILT_TIMER, EV_ADD, 0, timeout_ms, NULL); //0 means milliseconds (the default)
  } else if (timeout_ms == 0){
    /* libcurl wants us to timeout now, however setting both fields of
     * new_value.it_value to zero disarms the timer. The closest we can
     * do is to schedule the timer to fire in 1 ns. */
    EV_SET(&timer_event, id, EVFILT_TIMER, EV_ADD, NOTE_NSECONDS, 1, NULL);
  } else {
    // libcurl passes a -1 to indicate the timer should be deleted
    EV_SET(&timer_event, id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
  }

  if (kevent(lstate->epfd, &timer_event, 1, NULL, 0, NULL) < 0) {
    int save_errno = errno;
    ereport(ERROR, errmsg("kevent with EVFILT_TIMER failed: %s", strerror(save_errno)));
  }

  return 0;
}

int multi_socket_cb(__attribute__ ((unused)) CURL *easy, curl_socket_t sockfd, int what, LoopState *lstate, void *socketp) {
  static char *whatstrs[] = { "NONE", "CURL_POLL_IN", "CURL_POLL_OUT", "CURL_POLL_INOUT", "CURL_POLL_REMOVE" };
  elog(DEBUG2, "multi_socket_cb: sockfd %d received %s", sockfd, whatstrs[what]);

  SocketInfo *sock_info = (SocketInfo *)socketp;
  struct kevent ev[2];
  int  count = 0;

  if (what == CURL_POLL_REMOVE) {
    if (sock_info->action & CURL_POLL_IN)
      EV_SET(&ev[count++], sockfd, EVFILT_READ, EV_DELETE, 0, 0, sock_info);

    if (sock_info->action & CURL_POLL_OUT)
      EV_SET(&ev[count++], sockfd, EVFILT_WRITE, EV_DELETE, 0, 0, sock_info);

    curl_multi_assign(lstate->curl_mhandle, sockfd, NULL);
    pfree(sock_info);
  } else {
    if (!sock_info) {
      sock_info = palloc(sizeof(SocketInfo));
      sock_info->sockfd = sockfd;
      sock_info->action = what;
      curl_multi_assign(lstate->curl_mhandle, sockfd, sock_info);
    }

    if (what & CURL_POLL_IN)
      EV_SET(&ev[count++], sockfd, EVFILT_READ, EV_ADD, 0, 0, sock_info);

    if (what & CURL_POLL_OUT)
      EV_SET(&ev[count++], sockfd, EVFILT_WRITE, EV_ADD, 0, 0, sock_info);
  }

  Assert(count <= 2);

  if (kevent(lstate->epfd, &ev[0], count, NULL, 0, NULL) < 0) {
    int save_errno = errno;
    ereport(ERROR, errmsg("kevent with %s failed for sockfd %d: %s", whatstrs[what], sockfd, strerror(save_errno)));
  }

  return 0;
}

bool is_timer(event ev){
  return ev.filter == EVFILT_TIMER;
}

int get_curl_event(event ev){
  int ev_bitmask = 0;
  if (ev.filter == EVFILT_READ)
    ev_bitmask |= CURL_CSELECT_IN;
  else if (ev.filter == EVFILT_WRITE)
    ev_bitmask |= CURL_CSELECT_OUT;
  else
    ev_bitmask = CURL_CSELECT_ERR;

  return ev_bitmask;
}

int get_socket_fd(event ev){
  SocketInfo *sock_info = (SocketInfo *) ev.udata;

  return sock_info->sockfd;
}

#endif
