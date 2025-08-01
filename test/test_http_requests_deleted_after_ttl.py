import time

import pytest
from sqlalchemy import text

def test_http_responses_deleted_after_ttl(sess, autocommit_sess):
    """Check that http responses will be deleted when they reach their ttl, not immediately but when the worker wakes again"""

    autocommit_sess.execute(text("alter system set pg_net.ttl to '1 second'"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))

    # Create a request
    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            'http://localhost:8080/anything'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"

    # Sleep until after request should have been deleted
    time.sleep(1.1)

    # Wake the worker manually, under normal operation this will happen when new requests are received
    sess.execute(text("select net.wake()"))

    sess.commit() # commit so worker  wakes

    time.sleep(0.1) # wait for deletion

    # Ensure the response is now empty
    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert count == 0

    autocommit_sess.execute(text("alter system reset pg_net.ttl"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))


def test_http_responses_will_complete_deletion(sess, autocommit_sess):
    """Check that http responses will keep being deleted until completion despite no new requests coming"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,4) offset 3;
    """
    )).fetchone()

    sess.commit()

    # Collect the last response, waiting as needed
    response = sess.execute(
        text(
            """
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
        ).fetchone()
    assert response is not None
    assert response[0] == "SUCCESS"

    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response
    """
        )
    ).fetchone()
    assert count == 4

    autocommit_sess.execute(text("alter system set pg_net.ttl to '1 second';"))
    autocommit_sess.execute(text("alter system set pg_net.batch_size to 2;"))
    autocommit_sess.execute(text("select pg_reload_conf();"))

    # wait for ttl
    time.sleep(1)

    # Wake the worker manually, under normal operation this will happen when new requests are received
    sess.execute(text("select net.wake()"))
    sess.commit() # commit so worker  wakes

    time.sleep(0.1)

    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response
    """
        )
    ).fetchone()
    assert count == 2

    # wait for another batch
    time.sleep(1.1)

    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response
    """
        )
    ).fetchone()
    assert count == 0

    autocommit_sess.execute(text("alter system reset pg_net.ttl"))
    autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
    autocommit_sess.execute(text("select pg_reload_conf();"))


def test_http_responses_will_delete_despite_restart(sess, autocommit_sess):
    """Check that http responses will keep being despite no new requests coming" and despite restart"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,4) offset 3;
    """
    )).fetchone()

    sess.commit()

    # Collect the last response, waiting as needed
    response = sess.execute(
        text(
            """
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
        ).fetchone()
    assert response is not None
    assert response[0] == "SUCCESS"

    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response
    """
        )
    ).fetchone()
    assert count == 4

    # restart
    autocommit_sess.execute(text("alter system set pg_net.ttl to '1 second';"))
    autocommit_sess.execute(text("alter system set pg_net.batch_size to 2;"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))

    # wait for ttl
    time.sleep(1.1)

    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response
    """
        )
    ).fetchone()
    assert count == 0

    # reset
    autocommit_sess.execute(text("alter system reset pg_net.ttl"))
    autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))
