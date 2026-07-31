import time
from sqlalchemy import text
from common import collect_response_sync, http_request, http_requests, restart_worker
from common import wait_for_response_count, wakeup_worker


def test_http_responses_deleted_after_ttl(sess, autocommit_sess):
    """
    Check that http responses will be deleted when they reach their ttl,
    not immediately but when the worker wakes again
    """

    try:
        autocommit_sess.execute(
            text("alter system set pg_net.ttl to '1 second'"))
        restart_worker(autocommit_sess)

        request_id = http_request(sess, text(
            """
            select net.http_get(
                'http://localhost:8080/anything'
            );
        """
        ))

        response = collect_response_sync(sess, request_id)

        assert response is not None
        assert response["status"] == "SUCCESS"

        # Sleep a little more than ttl so that the request expires
        time.sleep(1.1)

        wakeup_worker(sess)

        # Check that the worker deleted the expired response
        wait_for_response_count(autocommit_sess, 0)

    finally:
        autocommit_sess.execute(text("alter system reset pg_net.ttl"))
        restart_worker(autocommit_sess)


def test_http_responses_will_complete_deletion(sess, autocommit_sess):
    """
    Check that http responses will keep being deleted
    until completion despite no new requests coming
    """

    request_id = http_requests(sess, text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,4) offset 3;
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"

    wait_for_response_count(autocommit_sess, 4)

    try:
        autocommit_sess.execute(
            text("alter system set pg_net.ttl to '1 second';"))
        autocommit_sess.execute(
            text("alter system set pg_net.batch_size to 2;"))
        autocommit_sess.execute(text("select pg_reload_conf();"))

        # Wait for ttl so that when we wakeup the worker it has
        # some expired responses to delete
        time.sleep(1)

        wakeup_worker(sess)

        # In one inner loop, the worker will delete batch size
        # worth of responses
        wait_for_response_count(autocommit_sess, 2)

        # But it will keep going as long as it had deleted
        # some responses. So after a wait of 1 second it
        # will delete another batch before going back to sleep
        wait_for_response_count(autocommit_sess, 0)

    finally:
        autocommit_sess.execute(text("alter system reset pg_net.ttl"))
        autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
        autocommit_sess.execute(text("select pg_reload_conf();"))


def test_http_responses_will_delete_despite_restart(sess, autocommit_sess):
    """
    Check that http responses will keep being deleted despite no
    new requests coming and despite worker restart
    """

    request_id = http_requests(sess, text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,4) offset 3;
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"

    wait_for_response_count(autocommit_sess, 4)

    try:
        # Restart the worker
        autocommit_sess.execute(
            text("alter system set pg_net.ttl to '1 second';"))
        autocommit_sess.execute(
            text("alter system set pg_net.batch_size to 2;"))
        restart_worker(autocommit_sess)

        # Wait for ttl so that the requests expire
        time.sleep(1.1)

        wait_for_response_count(autocommit_sess, 0)

    finally:
        # reset
        autocommit_sess.execute(text("alter system reset pg_net.ttl"))
        autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
        restart_worker(autocommit_sess)
