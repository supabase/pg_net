from sqlalchemy import text
from common import collect_response_sync, http_request


def test_http_headers_set(sess):
    """Check that headers are being set"""
    request_id = http_request(sess, text(
        """
        select net.http_get(
            url:='http://localhost:8080/headers',
            headers:='{"pytest-header": "pytest-header", "accept": "application/json"}'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert "pytest-header" in response["body"]
