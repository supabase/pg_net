
from common import collect_response_sync, http_request
from sqlalchemy import text
import time
import platform
import pytest

# TODO create a separate test that can run on macos
# TODO test should be linux only

def test_addr_restrict_multi_platform(autocommit_sess):
    """Test net.http_delete works, test runs on all platforms"""

    autocommit_sess.execute(text("alter system set pg_net.address_blacklist = '127.0.0.1';"))
    autocommit_sess.execute(text("select pg_reload_conf()"))

    time.sleep(1.6)
    request_id = http_request(
        autocommit_sess,
        text(
            """
        select net.http_delete(
            url:='http://127.0.0.1:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
        ),
    )

    response = collect_response_sync(autocommit_sess, request_id)

    assert response is not None
    assert response["status"] == "ERROR"
    assert response["message"] == "Could not connect to server"

    # /delete endpoint returns params and headers in the response body
    assert response["body"] is None

    autocommit_sess.execute(text("alter system reset pg_net.address_blacklist;"))
    autocommit_sess.execute(text("select pg_reload_conf();"))

    time.sleep(1.6)
    request_id = http_request(
        autocommit_sess,
        text(
            """
        select net.http_delete(
            url:='http://127.0.0.1:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
        ),
    )

    response = collect_response_sync(autocommit_sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"

@pytest.mark.skipif(platform.system() == "darwin", reason="Test requires multiple internal IP's")
def test_addr_restrict(autocommit_sess):
    """Test net.http_delete works"""

    autocommit_sess.execute(text("alter system set pg_net.address_blacklist = '127.0.0.1';"))
    autocommit_sess.execute(text("select pg_reload_conf()"))

    time.sleep(1.6)
    request_id = http_request(
        autocommit_sess,
        text(
            """
        select net.http_delete(
            url:='http://127.0.0.1:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
        ),
    )

    response = collect_response_sync(autocommit_sess, request_id)

    assert response is not None
    assert response["status"] == "ERROR"
    assert response["message"] == "Could not connect to server"

    # /delete endpoint returns params and headers in the response body
    assert response["body"] is None

    request_id = http_request(
        autocommit_sess,
        text(
            """
        select net.http_delete(
            url:='http://127.0.0.1:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
        ),
    )

    response = collect_response_sync(autocommit_sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"

    autocommit_sess.execute(text("alter system reset pg_net.address_blacklist;"))
    autocommit_sess.execute(text("select pg_reload_conf();"))

    time.sleep(1.6)
    request_id = http_request(
        autocommit_sess,
        text(
            """
        select net.http_delete(
            url:='http://127.0.0.1:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
        ),
    )

    response = collect_response_sync(autocommit_sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"


@pytest.mark.skipif(platform.system() == "darwin", reason="Test requires multiple internal IP's")
def test_addr_restrict_multi(autocommit_sess):
    """Test 2 addresses in blacklist"""

    autocommit_sess.execute(text("alter system set pg_net.address_blacklist = '127.0.0.1,127.0.0.2';"))
    autocommit_sess.execute(text("select pg_reload_conf()"))

    time.sleep(1.6)
    for expect_reject_addr in ["127.0.0.1", "127.0.0.2"]:
        request_id = http_request(
            autocommit_sess,
            text(
                f"""
            select net.http_delete(
                url:='http://{expect_reject_addr}:8080/delete'
            ,   params:= '{{"param-foo": "bar"}}'
            ,   headers:= '{{"X-Baz": "foo"}}'
            );
        """
            ),
        )

        response = collect_response_sync(autocommit_sess, request_id)

        assert response is not None
        assert response["status"] == "ERROR"
        assert response["message"] == "Could not connect to server"

        # /delete endpoint returns params and headers in the response body
        assert response["body"] is None

    autocommit_sess.execute(text("alter system reset pg_net.address_blacklist;"))
    autocommit_sess.execute(text("select pg_reload_conf();"))

    time.sleep(1.6)
    request_id = http_request(
        autocommit_sess,
        text(
            """
        select net.http_delete(
            url:='http://127.0.0.1:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
        ),
    )

    response = collect_response_sync(autocommit_sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
