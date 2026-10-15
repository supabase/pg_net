
from common import collect_response_sync, http_request
from sqlalchemy import text
import time
import platform
import pytest

# TODO create a separate test that can run on macos
# TODO test should be linux only


PLATFORM_IS_DARWIN = platform.system() == "Darwin"

@pytest.mark.parametrize(
        ("blacklist_entry", "error_addrs", "success_addrs"),
        [
            ("127.0.0.1", ["127.0.0.1"], []),
            ("127.0.0.1,127.0.0.2", ["127.0.0.1"], []),
            ("127.0.0.2,127.0.0.1", ["127.0.0.1"], []),
            pytest.param(
                "127.0.0.1", ["127.0.0.1"], ["127.0.0.2"],
                marks=pytest.mark.skipif(PLATFORM_IS_DARWIN, reason="not supported on darwin")
            ),
            pytest.param(
                "127.0.0.1,127.0.0.2", ["127.0.0.1", "127.0.0.2"], ["127.0.0.3"],
                marks=pytest.mark.skipif(PLATFORM_IS_DARWIN, reason="not supported on darwin")
            ),
        ]
    )
def test_addr_restrict_multi_platform(
        autocommit_sess, 
        blacklist_entry: str,
        error_addrs: list,
        success_addrs: list,
    ):
    """Test net.http_delete works, test runs on all platforms"""

    autocommit_sess.execute(text(f"alter system set pg_net.address_blacklist = '{blacklist_entry}';"))
    autocommit_sess.execute(text("select pg_reload_conf()"))
    time.sleep(1)

    for error_addr in error_addrs:
        request_id = http_request(
            autocommit_sess,
            text(
                f"""
            select net.http_delete(
                url:='http://{error_addr}:8080/delete'
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

    for addr in success_addrs:
        request_id = http_request(
            autocommit_sess,
            text(
                f"""
            select net.http_delete(
                url:='http://{addr}:8080/delete'
            ,   params:= '{{"param-foo": "bar"}}'
            ,   headers:= '{{"X-Baz": "foo"}}'
            );
        """
            ),
        )

        response = collect_response_sync(autocommit_sess, request_id)

        assert response is not None
        assert response["status"] == "SUCCESS"

    
    autocommit_sess.execute(text("alter system reset pg_net.address_blacklist;"))
    autocommit_sess.execute(text("select pg_reload_conf();"))
    time.sleep(1)

    for addr in error_addrs + success_addrs:
        request_id = http_request(
            autocommit_sess,
            text(
                f"""
            select net.http_delete(
                url:='http://{addr}:8080/delete'
            ,   params:= '{{"param-foo": "bar"}}'
            ,   headers:= '{{"X-Baz": "foo"}}'
            );
        """
            ),
        )

        response = collect_response_sync(autocommit_sess, request_id)

        assert response is not None
        assert response["status"] == "SUCCESS"

