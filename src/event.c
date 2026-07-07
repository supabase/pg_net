#include <errno.h>
#include <stddef.h>
#include <unistd.h>

#include "curl_prelude.h"
#include "pg_prelude.h"

#include "errors.h"
#include "event.h"

#ifdef WAIT_USE_EPOLL

static int  timerfd       = 0;
static bool timer_created = false;

typedef struct epoll_event epoll_event;
typedef struct itimerspec  itimerspec;

inline int wait_event(int fd, event *events, size_t maxevents, int timeout_milliseconds) {
  return epoll_wait(fd, events, maxevents, timeout_milliseconds);
}

inline int event_monitor() {
  return epoll_create1(0);
}

void ev_monitor_close(WorkerState *wstate) {
  close(wstate->epfd);
  close(timerfd);
}

int multi_timer_cb(__attribute__((unused)) CURLM *multi, long timeout_ms, void *userp) {
  WorkerState *wstate = (WorkerState *)userp;
  elog(DEBUG2, "multi_timer_cb: Setting timeout to %ld ms\n", timeout_ms);

  if (!timer_created) {
    timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd < 0) {
      ereport(ERROR, errmsg("Failed to create timerfd"));
    }
    timerfd_settime(timerfd, 0, &(itimerspec){}, NULL);
    epoll_ctl(wstate->epfd, EPOLL_CTL_ADD, timerfd,
              &(epoll_event){.events = EPOLLIN, .data.fd = timerfd});

    timer_created = true;
  }

  // disable clang-format as it only hurts readability here
  // clang-format off
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
  // clang-format on

  int no_flags = 0;
  if (timerfd_settime(timerfd, no_flags, &its, NULL) < 0) {
    ereport(ERROR, errmsg("timerfd_settime failed"));
  }

  return 0;
}

// A marker value only need to be assigned to a socket's socketp pointer via a call to
// curl_multi_assign.
static char socketp_marker;

int multi_socket_cb(__attribute__((unused)) CURL *easy, curl_socket_t sockfd, int what, void *userp,
                    void *socketp) {
  WorkerState *wstate     = (WorkerState *)userp;
  static char *whatstrs[] = {"NONE", "CURL_POLL_IN", "CURL_POLL_OUT", "CURL_POLL_INOUT",
                             "CURL_POLL_REMOVE"};
  elog(DEBUG2, "multi_socket_cb: sockfd %d received %s", sockfd, whatstrs[what]);

  // libcurl calls the multi_socket_cb with socketp set to null for a socketfd when it is first
  // created. At that time we set the socketp to the marker value via a call to curl_multi_assign so
  // that any subsequent calls will have that marker value set. This helps us distinguish between
  // EPOLL_CTL_ADD and EPOLL_CTL_MOD scenarios. We could have set the socketp to a heap allocated
  // value but since we don't need the actual value, we avoid those allocations and use the marker
  // value.
  int epoll_op;
  if (!socketp) {
    epoll_op = EPOLL_CTL_ADD;
    EREPORT_MULTI(curl_multi_assign(wstate->curl_mhandle, sockfd, &socketp_marker));
  } else if (what == CURL_POLL_REMOVE) {
    epoll_op = EPOLL_CTL_DEL;
    EREPORT_MULTI(curl_multi_assign(wstate->curl_mhandle, sockfd, NULL));
  } else {
    epoll_op = EPOLL_CTL_MOD;
  }

  epoll_event ev = {
    .data.fd = sockfd,
    // We can get the `what` variable value equal to either CURL_POLL_IN, CURL_POLL_OUT,
    // CURL_POLL_INOUT (equivalent to CURL_POLL_IN | CURL_POLL_OUT), or CURL_POLL_REMOVE. We want to
    // set the events member correspondingly to EPOLLIN, EPOLLOUT, EPOLLIN | EPOLLOUT, or 0 (ignored
    // when epoll_op is EPOLL_CTL_DEL).
    .events = (what & CURL_POLL_IN ? EPOLLIN : 0) | (what & CURL_POLL_OUT ? EPOLLOUT : 0),
  };

  // epoll_ctl will copy ev, so there's no need to do palloc for the epoll_event
  // https://github.com/torvalds/linux/blob/e32cde8d2bd7d251a8f9b434143977ddf13dcec6/fs/eventpoll.c#L2408-L2418
  if (epoll_ctl(wstate->epfd, epoll_op, sockfd, &ev) < 0) {
    int          e        = errno;
    static char *opstrs[] = {"NONE", "EPOLL_CTL_ADD", "EPOLL_CTL_DEL", "EPOLL_CTL_MOD"};
    ereport(ERROR, errmsg("epoll_ctl with %s failed when receiving %s for sockfd %d: %s",
                          whatstrs[what], opstrs[epoll_op], sockfd, strerror(e)));
  }

  return 0;
}

bool is_timer(event ev) {
  return ev.data.fd == timerfd;
}

int get_curl_event(event ev) {
  int ev_bitmask = ev.events & EPOLLIN    ? CURL_CSELECT_IN
                   : ev.events & EPOLLOUT ? CURL_CSELECT_OUT
                                          : CURL_CSELECT_ERR;
  return ev_bitmask;
}

int get_socket_fd(event ev) {
  return ev.data.fd;
}

#else

typedef struct {
  curl_socket_t sockfd;
  int           action;
} SocketInfo;

int inline wait_event(int fd, event *events, size_t maxevents, int timeout_milliseconds) {
  return kevent(fd, NULL, 0, events, maxevents,
                &(struct timespec){.tv_sec = timeout_milliseconds / 1000});
}

int inline event_monitor() {
  return kqueue();
}

void ev_monitor_close(WorkerState *wstate) {
  close(wstate->epfd);
}

int multi_timer_cb(__attribute__((unused)) CURLM *multi, long timeout_ms, void *userp) {
  WorkerState *wstate = (WorkerState *)userp;
  elog(DEBUG2, "multi_timer_cb: Setting timeout to %ld ms\n", timeout_ms);
  event timer_event;
  int   id = 1;

  if (timeout_ms > 0) {
    EV_SET(&timer_event, id, EVFILT_TIMER, EV_ADD, 0, timeout_ms,
           NULL); // 0 means milliseconds (the default)
  } else if (timeout_ms == 0) {
    /* libcurl wants us to timeout now, however setting both fields of
     * new_value.it_value to zero disarms the timer. The closest we can
     * do is to schedule the timer to fire in 1 ns. */
    EV_SET(&timer_event, id, EVFILT_TIMER, EV_ADD, NOTE_NSECONDS, 1, NULL);
  } else {
    // libcurl passes a -1 to indicate the timer should be deleted
    EV_SET(&timer_event, id, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
  }

  if (kevent(wstate->epfd, &timer_event, 1, NULL, 0, NULL) < 0) {
    int save_errno = errno;
    ereport(ERROR, errmsg("kevent with EVFILT_TIMER failed: %s", strerror(save_errno)));
  }

  return 0;
}

int multi_socket_cb(__attribute__((unused)) CURL *easy, curl_socket_t sockfd, int what, void *userp,
                    void *socketp) {
  WorkerState *wstate     = (WorkerState *)userp;
  static char *whatstrs[] = {"NONE", "CURL_POLL_IN", "CURL_POLL_OUT", "CURL_POLL_INOUT",
                             "CURL_POLL_REMOVE"};
  elog(DEBUG2, "multi_socket_cb: sockfd %d received %s", sockfd, whatstrs[what]);

  SocketInfo   *sock_info = (SocketInfo *)socketp;
  struct kevent ev[2];
  int           count = 0;

  if (!sock_info) {
    sock_info         = palloc(sizeof(SocketInfo));
    sock_info->sockfd = sockfd;
    sock_info->action = CURL_POLL_NONE;
    EREPORT_MULTI(curl_multi_assign(wstate->curl_mhandle, sockfd, sock_info));
  }

  UPDATE_FILTER(CURL_POLL_IN, EVFILT_READ);
  UPDATE_FILTER(CURL_POLL_OUT, EVFILT_WRITE);

  sock_info->action = what;

  if (what == CURL_POLL_REMOVE) {
    EREPORT_MULTI(curl_multi_assign(wstate->curl_mhandle, sockfd, NULL));
    pfree(sock_info);
  }

  Assert(count <= 2);

  if (kevent(wstate->epfd, &ev[0], count, NULL, 0, NULL) < 0) {
    int save_errno = errno;
    ereport(ERROR, errmsg("kevent with %s failed for sockfd %d: %s", whatstrs[what], sockfd,
                          strerror(save_errno)));
  }

  return 0;
}

bool is_timer(event ev) {
  return ev.filter == EVFILT_TIMER;
}

int get_curl_event(event ev) {
  int ev_bitmask = 0;
  if (ev.filter == EVFILT_READ)
    ev_bitmask |= CURL_CSELECT_IN;
  else if (ev.filter == EVFILT_WRITE)
    ev_bitmask |= CURL_CSELECT_OUT;
  else
    ev_bitmask = CURL_CSELECT_ERR;

  return ev_bitmask;
}

int get_socket_fd(event ev) {
  SocketInfo *sock_info = (SocketInfo *)ev.udata;

  return sock_info->sockfd;
}

#endif
