import time

import pytest
from sqlalchemy import text

def test_http_get_timeout_reached(sess):
    """net.http_get with timeout errs on a slow reply"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(url := 'http://localhost:9999/p/200:b"wait%20for%20three%20seconds"pr,3');
    """
    )).fetchone()

    sess.commit()

    time.sleep(3.5)

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
        select net.http_get(url := 'http://localhost:9999/p/200:b"wait%20for%20three%20seconds"pr,3', timeout_milliseconds := 3500);
    """
    )).fetchone()

    sess.commit()

    time.sleep(4)

    (response,) = sess.execute(
        text(
            """
        select content from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert 'wait for three seconds' in str(response)

def test_many_slow_mixed_with_fast(sess):
    """many fast responses finish despite being mixed with slow responses, the fast responses will wait the timeout duration"""

    sess.execute(text(
        """
        select
          net.http_get(url := 'http://localhost:9999/p/200')
        , net.http_get(url := 'http://localhost:9999/p/200:pr,f')
        , net.http_get(url := 'http://localhost:9999/p/200')
        , net.http_get(url := 'http://localhost:9999/p/200:pr,f')
        from generate_series(1,25) _;
    """
    ))

    sess.commit()

    time.sleep(3)

    (status_code,count) = sess.execute(text(
    """
        select status_code, count(*) from net._http_response where status_code = 200 group by status_code;
    """
    )).fetchone()

    assert status_code == 200
    assert count == 50
