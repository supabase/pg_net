from sqlalchemy import text
from common import collect_response_sync, http_request


def test_http_request_header_with_lf_is_rejected(sess):
    """
    A header value ending in a line feed must not be sent.

    libcurl appends the CRLF terminator itself, so an embedded LF closes the
    header early and the receiving parser reads whatever follows as more
    headers or as the body (request splitting).
    """

    request_id = http_request(
        sess,
        text(
            r"""
        select net.http_get(
            url:='http://localhost:8080/headers',
            headers:=jsonb_build_object('Injected', 'value' || chr(10) || 'X-Smuggled: 1')
        );
    """
        ),
    )

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "ERROR"
    assert "carriage return or line feed" in response["message"]
    # The header name is safe to surface; the value may hold credentials.
    assert "Injected" in response["message"]
    assert "X-Smuggled" not in response["message"]


def test_http_request_header_with_cr_is_rejected(sess):
    """A carriage return is rejected for the same reason as a line feed."""

    request_id = http_request(
        sess,
        text(
            r"""
        select net.http_get(
            url:='http://localhost:8080/headers',
            headers:=jsonb_build_object('Injected', 'value' || chr(13) || 'X-Smuggled: 1')
        );
    """
        ),
    )

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "ERROR"
    assert "carriage return or line feed" in response["message"]


def test_http_ordinary_headers_still_sent(sess):
    """The check must not affect headers that contain no CR or LF."""

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
    assert response["status"] == "SUCCESS"
    assert "pytest-header" in response["body"]
