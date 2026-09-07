import time
import pytest
from sqlalchemy import text
from sqlalchemy.exc import DBAPIError
from common import collect_response_sync, http_request, restart_worker
from common import wait_for_response_count, wakeup_worker


def reset_ttl(autocommit_sess):
    autocommit_sess.execute(text("alter system reset pg_net.ttl"))
    autocommit_sess.execute(text("select pg_reload_conf()"))


def test_invalid_ttl_is_rejected(autocommit_sess):
    """
    An invalid interval must be rejected at ALTER SYSTEM time
    instead of making the worker fail when it tries to delete
    expired responses (issue #268)
    """
    for bad in ["1 blah", "yesterday", "abc", "1 minute 10 potatoes"]:
        with pytest.raises(DBAPIError) as excinfo:
            autocommit_sess.execute(text(f"alter system set pg_net.ttl to '{bad}'"))
        msg = str(excinfo.value)
        assert 'invalid value for parameter "pg_net.ttl"' in msg
        assert "invalid input syntax for type interval" in msg

    # the setting must be untouched
    (ttl,) = autocommit_sess.execute(text("show pg_net.ttl")).fetchone()
    assert ttl == "6 hours"


def test_negative_ttl_is_rejected(autocommit_sess):
    """
    A negative ttl would expire every response immediately, reject it
    """
    for bad in ["-1 hour", "-10 seconds", "1 hour ago"]:
        with pytest.raises(DBAPIError) as excinfo:
            autocommit_sess.execute(text(f"alter system set pg_net.ttl to '{bad}'"))
        msg = str(excinfo.value)
        assert 'invalid value for parameter "pg_net.ttl"' in msg
        assert "negative interval" in msg


def test_valid_ttl_formats_are_accepted(autocommit_sess):
    """
    Any interval that postgres itself accepts must be accepted,
    including compound values (issue #269)
    """
    try:
        for good in [
            "1 minute 10 seconds",
            "90 seconds",
            "1.5 hours",
            "00:01:10",
            "1 day 2 hours 3 minutes",
            "500 milliseconds",
            "0",
        ]:
            autocommit_sess.execute(text(f"alter system set pg_net.ttl to '{good}'"))
            autocommit_sess.execute(text("select pg_reload_conf()"))
    finally:
        reset_ttl(autocommit_sess)


def test_compound_ttl_is_honored_by_worker(sess, autocommit_sess):
    """
    A compound interval like '1 second 500 milliseconds' must be
    applied by the worker as-is, i.e. a response must be expired
    once that time has passed and the worker wakes (issue #269)
    """
    try:
        autocommit_sess.execute(
            text("alter system set pg_net.ttl to '1 second 500 milliseconds'"))
        restart_worker(autocommit_sess)

        request_id = http_request(sess, text(
            "select net.http_get('http://localhost:8080/anything')"))

        response = collect_response_sync(sess, request_id)
        assert response is not None
        assert response["status"] == "SUCCESS"

        # Sleep past the ttl so the response is expired, then wake the worker
        time.sleep(1.6)
        wakeup_worker(sess)

        wait_for_response_count(autocommit_sess, 0)

    finally:
        reset_ttl(autocommit_sess)
        restart_worker(autocommit_sess)
