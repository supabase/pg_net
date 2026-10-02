
from common import collect_response_sync, http_request
from sqlalchemy import text
import time

def test_addr_restrict(autocommit_sess):
    """Test net.http_delete works"""

    autocommit_sess.execute(text("alter system set pg_net.address_blacklist = '127.0.0.1';"))
    autocommit_sess.execute(text("select pg_reload_conf()"))

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
