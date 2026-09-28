import pytest
from common import pg_collect_response, pg_http_request
from test_http_timeout import wait_for_responses


@pytest.mark.parametrize(
    "header, reported_name",
    [
        ('{"X-Test": "value\\n"}', "X-Test"),
        ('{"X-Test": "value\\r"}', "X-Test"),
        ('{"X-Test": "value\\r\\nInjected: yes"}', "X-Test"),
        ('{"X-Te\\nst": "value"}', "X-Te"),
        ('{"\\r\\nInjected": "yes"}', ""),
    ],
)
def test_headers_with_cr_or_lf_are_rejected(conn, header, reported_name):
    """A header containing CR or LF is not sent and gets an ERROR response naming the header"""

    request_id = pg_http_request(
        conn,
        "select net.http_get(url := 'http://localhost:8080/headers', headers := %s::jsonb)",
        (header,),
    )

    (status_code, error_msg, timed_out) = wait_for_responses(conn, [request_id])[
        request_id
    ]

    assert status_code is None
    assert timed_out is False
    assert (
        error_msg == f'header "{reported_name}" contains a carriage return or line feed'
    )
    assert "value" not in error_msg
    assert "Injected" not in error_msg


def test_headers_without_cr_or_lf_are_sent(conn):
    """Ordinary headers still reach the server"""

    request_id = pg_http_request(
        conn,
        """select net.http_get(
            url := 'http://localhost:8080/headers',
            headers := '{"X-Test": "plain value", "accept": "application/json"}'::jsonb
        )""",
    )

    response = pg_collect_response(conn, request_id)

    assert response["status"] == "SUCCESS"
    assert "X-Test" in response["body"]


def test_rejected_header_does_not_affect_the_rest_of_the_batch(conn):
    """One bad header in a batch gets an ERROR row, the other requests are sent normally"""

    bad = pg_http_request(
        conn,
        """select net.http_get(url := 'http://localhost:8080/headers', headers := '{"X-Test": "a\\nb"}'::jsonb)""",
    )
    get = pg_http_request(
        conn, "select net.http_get(url := 'http://localhost:8080/headers')"
    )
    post = pg_http_request(
        conn,
        "select net.http_post(url := 'http://localhost:8080/anything', body := '{}'::jsonb)",
    )

    responses = wait_for_responses(conn, [bad, get, post])

    assert responses[bad][0] is None
    assert (
        responses[bad][1] == 'header "X-Test" contains a carriage return or line feed'
    )
    assert responses[get][0] == 200
    assert responses[post][0] == 200


def test_direct_insert_with_cr_or_lf_header_is_rejected(conn):
    """Rows inserted straight into the queue go through the same check"""

    request_id = pg_http_request(
        conn,
        """insert into net.http_request_queue(method, url, headers, timeout_milliseconds)
           values ('GET', 'http://localhost:8080/headers', '{"X-Test": "a\\rb"}'::jsonb, 5000)
           returning id""",
    )
    conn.execute("select net.wake()")
    conn.commit()

    (status_code, error_msg, _) = wait_for_responses(conn, [request_id])[request_id]

    assert status_code is None
    assert error_msg == 'header "X-Test" contains a carriage return or line feed'

    follow_up = pg_http_request(
        conn, "select net.http_get(url := 'http://localhost:8080/headers')"
    )
    assert wait_for_responses(conn, [follow_up])[follow_up][0] == 200


def test_timeout_rejection_is_reported_before_the_header_one(conn):
    """A request with both a bad timeout and a bad header gets the timeout message"""

    request_id = pg_http_request(
        conn,
        """select net.http_get(
            url := 'http://localhost:8080/headers',
            headers := '{"X-Test": "a\\nb"}'::jsonb,
            timeout_milliseconds := 0
        )""",
    )

    (_, error_msg, _) = wait_for_responses(conn, [request_id])[request_id]

    assert (
        error_msg
        == "timeout_milliseconds must be between 1 and 600000 (pg_net.max_timeout_ms), got 0"
    )
