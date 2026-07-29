from sqlalchemy import text
from common import collect_response_sync


def test_http_headers_set(sess):
    """Check that headers are being set"""
    # Create a request
    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            url:='http://localhost:8080/headers',
            headers:='{"pytest-header": "pytest-header", "accept": "application/json"}'
        );
    """
    )).fetchone()

    # Commit to wakeup background worker
    sess.commit()

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert "pytest-header" in response["body"]
