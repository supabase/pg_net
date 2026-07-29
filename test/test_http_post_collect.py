import json

from sqlalchemy import text
from common import collect_response_sync, http_request


def test_http_post_returns_id(sess):
    """Test net.http_post returns an id"""

    (request_id,) = sess.execute(text(
        """
        select net.http_post(
            url:='http://localhost:8080/post',
            body:='{}'::jsonb
        );
    """
    )).fetchone()

    assert request_id == 1


def test_http_post_special_chars_body(sess):
    """Test net.http_post returns an id"""

    (request_id,) = sess.execute(text(
        """
        select net.http_post(
            url:='http://localhost:8080/post',
            body:=json_build_object('foo', 'ba"r')::jsonb
        );
    """
    )).fetchone()

    assert request_id == 1


def test_http_post_collect_sync_success(sess):
    """Collect a response, waiting if it has not completed yet"""

    (request_id,) = http_request(sess, text(
        """
        select net.http_post(
            url:='http://localhost:8080/post'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"
    assert response["body"] is not None


def test_http_post_collect_non_empty_body(sess):
    """Collect a response async before completed"""

    (request_id,) = http_request(sess, text(
        """
        select net.http_post(
            url:='http://localhost:8080/post',
            body:='{"hello": "world"}'::jsonb,
            headers:='{"Content-Type": "application/json", "accept": "application/json"}'::jsonb
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"
    assert response["body"] is not None
    # Assert that response is json
    assert json.loads(response["body"])["hello"] == "world"


def test_http_post_wrong_header_exception(sess):
    """Confirm that non application/json raises exception"""

    did_raise = False

    try:
        sess.execute(text(
            """
            select net.http_post(
                url:='http://localhost:8080/post',
                headers:='{"Content-Type": "application/text"}'::jsonb
            );
        """
        )).fetchone()
    except:
        sess.rollback()
        did_raise = True

    assert did_raise


def test_http_post_no_content_type_coerce(sess):
    """Confirm that a missing content type coerces to application/json"""

    request_id, = sess.execute(text(
        """
        select net.http_post(
            url:='http://localhost:8080/post',
            headers:='{"other": "val"}'::jsonb
        );
    """
    )).fetchone()

    headers, = sess.execute(text(
        """
        select
            headers
        from
            net.http_request_queue
        where
            id = :request_id
    """), {"request_id": request_id}
    ).fetchone()

    assert headers["Content-Type"] == "application/json"
    assert headers["other"] == "val"


def test_http_post_empty_body(sess):
    """Test net.http_post can post a null body"""

    (request_id,) = http_request(sess, text(
        """
        select net.http_post(
            url:='http://localhost:8080/echo-method',
            body:=null
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["body"] == "POST\n"
