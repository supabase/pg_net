from sqlalchemy import text
from common import collect_response_sync, http_request


def test_http_get_url_params_set(sess):
    """Check that params are being set on GET"""
    request_id = http_request(sess, text(
        """
        select net.http_get(
            url:='http://localhost:8080/anything',
            params:='{"hello": "world"}'::jsonb
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert "?hello=world" in response["body"]


def test_http_post_url_params_set(sess):
    """Check that params are being set on POST"""
    request_id = http_request(sess, text(
        """
        select net.http_post(
            url:='http://localhost:8080/anything',
            params:='{"hello": "world"}'::jsonb
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert "?hello=world" in response["body"]
