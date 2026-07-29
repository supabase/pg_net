from sqlalchemy import text
import time

from common import collect_response_sync, http_request


def test_http_get_returns_id(sess):
    """Test net.http_get returns an id"""

    request_id = http_request(sess, text(
        """
        select net.http_get('http://localhost:8080');
    """
    ))

    assert request_id == 1


def test_http_get_works_with_ip(sess):
    """Test net.http_get returns an id when using an IP with port"""

    request_id = http_request(sess, text(
        """
        select net.http_get('http://127.0.0.1:8080');
    """
    ))

    assert request_id == 1


def test_http_get_collect_sync_success(sess):
    """Collect a response, waiting if it has not completed yet"""

    request_id = http_request(sess, text(
        """
        select net.http_get('http://localhost:8080');
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["message"] == "ok"
    assert response["body"] is not None
    assert response["status_code"] == 200


def test_http_collect_response_async_does_not_exist(sess):
    """Collect a non-existent response with async true"""

    # Collect the response, waiting as needed
    response = sess.execute(text(
        """
        select * from net._http_collect_response(1, async:=true);
    """
    )).fetchone()

    assert response[0] == "ERROR"
    assert "not found" in response[1]
    assert response[2] is None


def test_http_get_responses_have_different_created_times(sess):
    """Ensure the rows in net._http_response have different created times"""

    http_request(sess, text(
        """
        select net.http_get('http://localhost:8080/echo-method')
    """
    ))

    time.sleep(1)

    http_request(sess, text(
        """
        select net.http_get('http://localhost:8080/echo-method')
    """
    ))

    time.sleep(1)

    http_request(sess, text(
        """
        select net.http_get('http://localhost:8080/echo-method')
    """
    ))

    time.sleep(1)

    count = sess.execute(text(
        """
        select count(distinct created) from net._http_response;
    """
    )).scalar()

    assert count == 3


def test_http_get_collect_with_redirect(sess):
    """Test pg_net follows a redirect and collects a response"""

    request_id = http_request(sess, text(
        """
        select net.http_get('http://localhost:8080/redirect_me');
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["body"] == "I got redirected\n"


def test_http_get_ipv6(sess):
    """Test pg_net can resolve an ipv6 only server"""

    request_id = http_request(sess, text(
        """
        select net.http_get('http://localhost:8888/');
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["body"] == "Hello ipv6 only\n"


def test_http_get_null_headers(sess):
    """Test net.http_get can have null headers"""

    request_id = http_request(sess, text(
        """
        select net.http_get('http://localhost:8080', null::jsonb, null::jsonb, 100);
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["body"] == "Hello world\n"
