import time
import re
import psycopg
import pytest
from sqlalchemy import text
from common import http_request, pg_collect_response, pg_http_request


def test_http_get_timeout_reached(sess):
    """Test net.http_get with timeout errs on a slow reply"""

    request_id = http_request(
        sess,
        text(
            """
        select net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=6');
    """
        ),
    )

    # wait for timeout
    time.sleep(7)

    (content_type, content, response, timed_out) = sess.execute(
        text(
            """
        select content_type, content, error_msg, timed_out from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert content_type is None
    assert content is None
    assert timed_out
    assert response.startswith("Timeout of 5000 ms reached")


def test_http_detailed_timeout(sess):
    """Test the timeout shows a detailed error msg"""

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
    request_id = http_request(
        sess,
        text(
            """
        select net.http_get(url := 'http://localhost:8080/pathological?delay=1', timeout_milliseconds := 1000)
    """
        ),
    )

    # wait for timeout
    time.sleep(2.1)

    (content_type, content, response, timed_out) = sess.execute(
        text(
            """
        select content_type, content, error_msg, timed_out from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    match = regex.search(response)

    total_time = float(match.group("A"))
    dns_time = float(match.group("B"))
    tcp_ssl_time = float(match.group("C"))
    http_time = float(match.group("D"))

    assert content_type is None
    assert content is None
    assert timed_out
    assert total_time > 0
    assert dns_time > 0
    assert tcp_ssl_time > 0
    assert http_time > 0


def test_http_get_succeed_with_gt_timeout(sess):
    """
    Test net.http_get with timeout succeeds when the timeout
    is greater than the slow reply response time
    """

    request_id = http_request(
        sess,
        text(
            """
        select net.http_get(url := 'http://localhost:8080?status=200&delay=3', timeout_milliseconds := 3500);
    """
        ),
    )

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
    """
    Test many fast responses finish despite being mixed with slow responses,
    the fast responses will wait the timeout duration
    """

    sess.execute(
        text(
            """
      select
        net.http_get(url := 'http://localhost:8080/pathological?status=200')
      , net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=2', timeout_milliseconds := 1000)
      , net.http_get(url := 'http://localhost:8080/pathological?status=200')
      , net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=2', timeout_milliseconds := 1000)
      from generate_series(1,25) _;
    """
        )
    )

    sess.commit()

    # wait for timeouts
    time.sleep(3)

    (request_successes, request_timeouts) = sess.execute(
        text(
            """
      select
        count(*) filter (where error_msg is null and status_code = 200) as request_successes,
        count(*) filter (where error_msg is not null and error_msg like 'Timeout of 1000 ms reached%') as request_timeouts
      from net._http_response;
    """
        )
    ).fetchone()

    assert request_successes == 50
    assert request_timeouts == 50


@pytest.mark.parametrize("timeout", [0, -1, 2147483647])
def test_out_of_range_timeouts_are_rejected(conn, timeout):
    """A 0, negative or oversized timeout is not sent, the request gets an error response instead"""

    request_id = pg_http_request(
        conn,
        "select net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := %s)",
        (timeout,),
    )

    response = pg_collect_response(conn, request_id)

    assert response["status"] == "ERROR"
    assert (
        response["message"]
        == f"timeout_milliseconds must be between 1 and 600000 (pg_net.max_timeout_ms), got {timeout}"
    )


def test_worker_honours_max_timeout_ms(conn):
    """Lowering pg_net.max_timeout_ms lowers the bound the worker applies"""

    admin = psycopg.connect("dbname=postgres", autocommit=True)
    admin.execute("alter system set pg_net.max_timeout_ms = 2000")
    admin.execute("select pg_reload_conf()")
    time.sleep(1)

    try:
        request_id = pg_http_request(
            conn,
            "select net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := 5000)",
        )

        response = pg_collect_response(conn, request_id)

        assert response["status"] == "ERROR"
        assert (
            response["message"]
            == "timeout_milliseconds must be between 1 and 2000 (pg_net.max_timeout_ms), got 5000"
        )
    finally:
        admin.execute("alter system reset pg_net.max_timeout_ms")
        admin.execute("select pg_reload_conf()")
        admin.close()
        time.sleep(1)


def test_max_timeout_ms_is_superuser_only(conn):
    """Only superusers may change the bound"""

    conn.execute("set role pre_existing")

    with pytest.raises(psycopg.errors.InsufficientPrivilege):
        conn.execute("set pg_net.max_timeout_ms = 5")

    conn.rollback()


def test_rejected_requests_do_not_affect_the_rest_of_the_batch(conn):
    """Rejected and valid requests consumed in the same batch are all answered"""

    ids = list(
        conn.execute(
            """
        select
            net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := 0),
            net.http_get(url := 'http://localhost:8080/pathological?status=200'),
            net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := -1),
            net.http_get(url := 'http://localhost:8080/pathological?status=200')
        """
        ).fetchone()
    )
    conn.commit()

    deadline = time.time() + 15
    responses = {}
    while len(responses) < 4 and time.time() < deadline:
        for request_id, status_code, error_msg in conn.execute(
            "select id, status_code, error_msg from net._http_response where id = any(%s)",
            (ids,),
        ).fetchall():
            responses[request_id] = (status_code, error_msg)
        conn.rollback()
        time.sleep(0.2)

    assert [responses.get(i) for i in ids] == [
        (
            None,
            "timeout_milliseconds must be between 1 and 600000 (pg_net.max_timeout_ms), got 0",
        ),
        (200, None),
        (
            None,
            "timeout_milliseconds must be between 1 and 600000 (pg_net.max_timeout_ms), got -1",
        ),
        (200, None),
    ]


REJECTED = (
    "timeout_milliseconds must be between 1 and 600000 (pg_net.max_timeout_ms), got {}"
)


def wait_for_responses(conn, ids, deadline_s=20):
    """Poll net._http_response until every id has a row, return {id: (status_code, error_msg, timed_out)}"""
    deadline = time.time() + deadline_s
    responses = {}
    while len(responses) < len(ids) and time.time() < deadline:
        for request_id, status_code, error_msg, timed_out in conn.execute(
            "select id, status_code, error_msg, timed_out from net._http_response where id = any(%s)",
            (list(ids),),
        ).fetchall():
            responses[request_id] = (status_code, error_msg, timed_out)
        conn.rollback()
        time.sleep(0.2)
    return responses


@pytest.mark.parametrize("timeout", [1, 600000])
def test_timeout_bounds_are_accepted(conn, timeout):
    """The bounds themselves are valid, the request is sent"""

    request_id = pg_http_request(
        conn,
        "select net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := %s)",
        (timeout,),
    )

    (status_code, error_msg, timed_out) = wait_for_responses(conn, [request_id])[
        request_id
    ]

    # 1ms may legitimately time out, but the request must have been sent, not rejected
    assert status_code == 200 or timed_out
    assert error_msg != REJECTED.format(timeout)


def test_batch_with_only_rejected_requests_keeps_the_worker_alive(conn):
    """A batch where every request is rejected is answered and the worker carries on"""

    ids = list(
        conn.execute(
            """
        select
            net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := 0),
            net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := 0)
        """
        ).fetchone()
    )
    conn.commit()

    responses = wait_for_responses(conn, ids)
    assert [responses[i][1] for i in ids] == [REJECTED.format(0), REJECTED.format(0)]

    follow_up = pg_http_request(
        conn,
        "select net.http_get(url := 'http://localhost:8080/pathological?status=200')",
    )
    assert wait_for_responses(conn, [follow_up])[follow_up][0] == 200


def test_rejected_request_last_in_batch(conn):
    """A rejected request at the end of a batch does not disturb the ones before it"""

    ids = list(
        conn.execute(
            """
        select
            net.http_get(url := 'http://localhost:8080/pathological?status=200'),
            net.http_post(url := 'http://localhost:8080/pathological?status=200', body := '{}'),
            net.http_delete(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := 0)
        """
        ).fetchone()
    )
    conn.commit()

    responses = wait_for_responses(conn, ids)
    assert [responses[i][:2] for i in ids] == [
        (200, None),
        (200, None),
        (None, REJECTED.format(0)),
    ]


def test_direct_insert_out_of_range_is_rejected(conn):
    """Rows inserted directly into the queue go through the same check"""

    request_id = pg_http_request(
        conn,
        """
        insert into net.http_request_queue(method, url, timeout_milliseconds)
        values ('GET', 'http://localhost:8080/pathological?status=200', -7)
        returning id
    """,
    )
    conn.execute("select net.wake()")
    conn.commit()

    assert wait_for_responses(conn, [request_id])[request_id][1] == REJECTED.format(-7)


def test_rejected_alongside_slow_and_fast_requests(conn):
    """Rejection, a real timeout and a success in one batch are all recorded correctly"""

    ids = list(
        conn.execute(
            """
        select
            net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=6', timeout_milliseconds := 1500),
            net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := 0),
            net.http_get(url := 'http://localhost:8080/pathological?status=200')
        """
        ).fetchone()
    )
    conn.commit()

    responses = wait_for_responses(conn, ids)
    slow, rejected, fast = (responses[i] for i in ids)
    assert slow[2] and slow[1].startswith("Timeout of 1500 ms reached")
    assert rejected == (None, REJECTED.format(0), False)
    assert fast == (200, None, False)


def test_large_batch_mixing_rejected_and_valid_requests(conn):
    """More requests than batch_size, every other one rejected, all answered across batches"""

    ids = [
        row[0]
        for row in conn.execute(
            """
        select net.http_get(
            url := 'http://localhost:8080/pathological?status=200',
            timeout_milliseconds := case when mod(n, 2) = 0 then 0 else 5000 end)
        from generate_series(1, 250) n
        """
        ).fetchall()
    ]
    conn.commit()

    responses = wait_for_responses(conn, ids, deadline_s=60)
    assert len(responses) == 250
    assert all(
        responses[i][1] == REJECTED.format(0)
        for n, i in enumerate(ids, 1)
        if n % 2 == 0
    )
    assert all(responses[i][0] == 200 for n, i in enumerate(ids, 1) if n % 2 == 1)


def test_worker_restart_after_rejections(conn):
    """Rejections leave nothing behind that breaks a worker restart"""

    request_id = pg_http_request(
        conn,
        "select net.http_get(url := 'http://localhost:8080/pathological?status=200', timeout_milliseconds := 0)",
    )
    assert wait_for_responses(conn, [request_id])[request_id][1] == REJECTED.format(0)

    conn.execute("select net.worker_restart()")
    conn.execute("select net.wait_until_running()")
    conn.commit()

    follow_up = pg_http_request(
        conn,
        "select net.http_get(url := 'http://localhost:8080/pathological?status=200')",
    )
    assert wait_for_responses(conn, [follow_up])[follow_up][0] == 200
