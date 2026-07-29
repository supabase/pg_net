import json

from sqlalchemy import text
from common import collect_response_sync


def test_http_delete_returns_id(sess):
    """Test net.http_delete returns an id"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            url:='http://localhost:8080/delete'
        );
    """
    )).fetchone()

    assert request_id == 1


def test_http_delete_collect_sync_success(sess):
    """Test net.http_delete works"""

    # Create a request
    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            url:='http://localhost:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
    )).fetchone()

    # Commit to wakeup background worker
    sess.commit()

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"
    assert response["body"] is not None
    assert "X-Baz" in response["body"]
    assert "param-foo" in response["body"]


def test_http_delete_positional_args(sess):
    """
    Test net.http_delete works with positional arguments.
    This to ensure backwards compat when a new parameter is added to the function.
    """

    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete'
        );
    """
    )).fetchone()

    # Commit to wakeup background worker
    sess.commit()

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"

    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}'
        );
    """
    )).fetchone()

    # Commit to wakeup background worker
    sess.commit()

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"

    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}',
            '{"X-Baz": "foo"}'
        );
    """
    )).fetchone()

    # Commit to wakeup background worker
    sess.commit()

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"

    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}',
            '{"X-Baz": "foo"}',
            5000
        );
    """
    )).fetchone()

    # Commit to wakeup background worker
    sess.commit()

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"


def test_http_delete_with_body(sess):
    """Test delete with request body works"""

    # Create a request
    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            url  :='http://localhost:8080/delete_w_body'
        ,   body := '{"key": "val"}'
        );
    """
    )).fetchone()

    # Commit to wakeup background worker
    sess.commit()

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["body"] is not None
    assert json.loads(response["body"])["key"] == "val"
