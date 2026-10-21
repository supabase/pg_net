from sqlalchemy import text
from common import collect_response_sync, http_request


def test_http_headers_set(sess):
    """Check that headers are being set"""
    request_id = http_request(
        sess,
        text(
            """
        select net.http_get(
            url:='http://localhost:8080/headers',
            headers:='{"pytest-header": "pytest-header", "accept": "application/json"}'
        );
    """
        ),
    )

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert "pytest-header" in response["body"]


def test_http_headers_default_user_agent(sess):
    """Check that the pg_net User-Agent is sent by default"""
    request_id = http_request(
        sess,
        text(
            """
        select net.http_get(
            url:='http://localhost:8080/headers'
        );
    """
        ),
    )

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert "User-Agent: pg_net/" in response["body"]


def test_http_headers_override_user_agent(sess):
    """Check that a User-Agent header replaces the pg_net one instead of being sent along with it"""
    request_id = http_request(
        sess,
        text(
            """
        select net.http_get(
            url:='http://localhost:8080/headers',
            headers:='{"user-agent": "my-app/1.0"}'
        );
    """
        ),
    )

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert "user-agent: my-app/1.0" in response["body"]
    assert "pg_net/" not in response["body"]
