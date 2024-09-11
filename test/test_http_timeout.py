import time

import pytest
from sqlalchemy import text

def test_http_get_timeout_reached(sess):
    """net.http_get with timeout errs on a slow reply"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=6');
    """
    )).fetchone()

    sess.commit()

    # wait for timeout
    time.sleep(6)

    (response,) = sess.execute(
        text(
            """
        select error_msg from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert response == u'Timeout was reached'

def test_http_get_succeed_with_gt_timeout(sess):
    """net.http_get with timeout succeeds when the timeout is greater than the slow reply response time"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(url := 'http://localhost:8080?status=200&delay=3', timeout_milliseconds := 3500);
    """
    )).fetchone()

    sess.commit()

    time.sleep(4)

    (status_code,) = sess.execute(
        text(
            """
        select status_code from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert status_code == 200

def test_many_slow_mixed_with_fast(sess):
    """many fast responses finish despite being mixed with slow responses, the fast responses will wait the timeout duration"""

    sess.execute(text(
    """
      select
        net.http_get(url := 'http://localhost:8080/pathological?status=200')
      , net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=2', timeout_milliseconds := 1000)
      , net.http_get(url := 'http://localhost:8080/pathological?status=200')
      , net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=2', timeout_milliseconds := 1000)
      from generate_series(1,25) _;
    """
    ))

    sess.commit()

    # wait for timeouts
    time.sleep(3)

    (request_successes, request_timeouts) = sess.execute(text(
    """
      select
        count(*) filter (where error_msg is null and status_code = 200) as request_successes,
        count(*) filter (where error_msg is not null and error_msg like 'Timeout was reached') as request_timeouts
      from net._http_response;
    """
    )).fetchone()

    assert request_successes == 50
    assert request_timeouts == 50
