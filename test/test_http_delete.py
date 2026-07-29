import json
from sqlalchemy import text
from common import collect_response_sync, http_request


def test_http_delete_returns_id(sess):
    """Test net.http_delete returns an id"""

    request_id = http_request(sess, text(
        """
        select net.http_get(
            url:='http://localhost:8080/delete'
        );
    """
    ))

    assert request_id == 1


def test_http_delete_collect_sync_success(sess):
    """Test net.http_delete works"""

    request_id = http_request(sess, text(
        """
        select net.http_delete(
            url:='http://localhost:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"

    # /delete endpoint returns params and headers in the response body
    assert response["body"] is not None
    assert "X-Baz" in response["body"]
    assert "param-foo" in response["body"]


def test_http_delete_positional_args(sess):
    """
    Test net.http_delete works with positional arguments.
    This to ensure backwards compat when a new parameter is added to the function.
    """

    # Delete call with url only
    request_id = http_request(sess, text(
        """
        select net.http_delete(
            'http://localhost:8080/delete'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"

    # Delete call with url and params
    request_id = http_request(sess, text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"

    # Delete call with url, params, and headers
    request_id = http_request(sess, text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}',
            '{"X-Baz": "foo"}'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"

    # Delete call with url, params, headers, and timeout
    request_id = http_request(sess, text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}',
            '{"X-Baz": "foo"}',
            5000
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"


def test_http_delete_with_body(sess):
    """Test delete with request body works"""

    request_id = http_request(sess, text(
        """
        select net.http_delete(
            url  :='http://localhost:8080/delete_w_body'
        ,   body := '{"key": "val"}'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["body"] is not None
    assert json.loads(response["body"])["key"] == "val"
