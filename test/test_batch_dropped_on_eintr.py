import time
from sqlalchemy import text
from common import http_requests, wait_for_response_count, wait_for_queue_drain


def test_batch_is_not_dropped_when_first_wait_event_is_interrupted(
    sess, autocommit_sess
):
    """
    A signal arriving while the worker sits in its first wait_event() of a
    batch makes it return EINTR. The whole batch (all 5 requests, which fit
    in a single batch with the default batch_size) must still be processed
    and produce a response each.

    This only reproduces deterministically against a worker built with the
    delay hack in multi_timer_cb (a 0ms libcurl timeout is turned into 2s),
    which keeps the first wait_event() blocked long enough to interrupt it.
    Without the hack the window is only microseconds wide and the test
    passes trivially.
    """
    http_requests(
        sess,
        text(
            "select net.http_get('http://localhost:8080/pathological?status=200') "
            "from generate_series(1,5)"
        ),
    )

    # Give the worker time to consume the row and block in wait_event()
    time.sleep(0.5)

    # pg_reload_conf() makes the postmaster send SIGHUP to the worker, which
    # interrupts epoll_wait() with EINTR
    autocommit_sess.execute(text("select pg_reload_conf();"))

    wait_for_queue_drain(autocommit_sess)
    wait_for_response_count(autocommit_sess, 5)
