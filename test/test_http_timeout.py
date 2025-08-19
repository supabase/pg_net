import time

import pytest
import re
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
    time.sleep(7)

    (content_type, content, response,timed_out) = sess.execute(
        text(
            """
        select content_type, content, error_msg, timed_out from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert content_type == None
    assert content == None
    assert timed_out == True
    assert response.startswith("Timeout of 5000 ms reached")


def test_http_detailed_timeout(sess):
    """the timeout shows a detailed error msg"""

    pattern = r"""
        Total\stime:\s*                     # Match 'Total time:' with optional spaces
        (?P<A>[0-9]*\.?[0-9]+)\s*ms         # Capture A (Total time)
        \s*\(DNS\stime:\s*                  # Match '(DNS time:' with optional spaces
        (?P<B>[0-9]*\.?[0-9]+)\s*ms,\s*     # Capture B (DNS time)
        TCP/SSL\shandshake\stime:\s*        # Match 'TCP/SSL handshake time:' with spaces
        (?P<C>[0-9]*\.?[0-9]+)\s*ms,\s*     # Capture C (TCP/SSL handshake time)
        HTTP\sRequest/Response\stime:\s*    # Match 'HTTP Request/Response time:' with spaces
        (?P<D>[0-9]*\.?[0-9]+)\s*ms\)       # Capture D (HTTP Request/Response time)
    """

    regex = re.compile(pattern, re.VERBOSE)

    # TODO Timeout at the DNS step.
    # TODO make this work locally. A slow DNS cannot be ensured on an external network.
    # This can be done manually with `select net.http_get('https://news.ycombinator.com/', timeout_milliseconds := 10);`

    # TODO add a TCP/SSL handshake timeout test
    # This can be done locally on Linux with `sudo tc qdisc add dev lo root netem delay 500ms` and
    # select net.http_get(url := 'http://localhost:8080/pathological', timeout_milliseconds := 1000);

    # Timeout at the HTTP step
    (request_id,) = sess.execute(text(
        """
        select net.http_get(url := 'http://localhost:8080/pathological?delay=1', timeout_milliseconds := 1000)
    """
    )).fetchone()

    sess.commit()

    # wait for timeout
    time.sleep(2.1)

    (content_type, content, response,timed_out) = sess.execute(
        text(
            """
        select content_type, content, error_msg, timed_out from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    match = regex.search(response)

    total_time   = float(match.group('A'))
    dns_time     = float(match.group('B'))
    tcp_ssl_time = float(match.group('C'))
    http_time    = float(match.group('D'))

    assert content_type == None
    assert content == None
    assert timed_out == True
    assert total_time > 0
    assert dns_time > 0
    assert tcp_ssl_time > 0
    assert http_time > 0

def test_http_get_succeed_with_gt_timeout(sess):
    """net.http_get with timeout succeeds when the timeout is greater than the slow reply response time"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(url := 'http://localhost:8080?status=200&delay=3', timeout_milliseconds := 3500);
    """
    )).fetchone()

    sess.commit()

    time.sleep(4.5)

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
        count(*) filter (where error_msg is not null and error_msg like 'Timeout of 1000 ms reached%') as request_timeouts
      from net._http_response;
    """
    )).fetchone()

    assert request_successes == 50
    assert request_timeouts == 50
