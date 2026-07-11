import pytest
from sqlalchemy import text

def test_http_responses_deleted_after_ttl(sess, autocommit_sess, wait_until):
    """Check that http responses will be deleted when they reach their ttl, not immediately but when the worker wakes again"""

    autocommit_sess.execute(text("alter system set pg_net.ttl to '1 second'"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))

    try:
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

        # Wait until the request's age exceeds the ttl
        wait_until(
            fetch=lambda: sess.execute(
                text("select extract(epoch from clock_timestamp() - created) from net._http_response where id = :request_id"),
                {"request_id": request_id},
            ).scalar(),
            predicate=lambda age: age is None or age >= 1.0,
            timeout=5,
            description=f"request {request_id} to exceed the ttl",
        )

        # Wake the worker manually, under normal operation this will happen when new requests are received
        sess.execute(text("select net.wake()"))

        sess.commit() # commit so worker  wakes

        # Ensure the response is now empty
        (count,) = wait_until(
            fetch=lambda: sess.execute(
                text("select count(*) from net._http_response where id = :request_id"),
                {"request_id": request_id},
            ).fetchone(),
            predicate=lambda r: r[0] == 0,
            timeout=5,
            description=f"deletion of expired response {request_id}",
        )
        assert count == 0
    finally:
        # Always restore ttl, even on assertion failure - it's set via ALTER
        # SYSTEM so it would otherwise leak into and destabilize later tests.
        autocommit_sess.execute(text("alter system reset pg_net.ttl"))
        autocommit_sess.execute(text("select net.worker_restart()"))
        autocommit_sess.execute(text("select net.wait_until_running()"))


def test_http_responses_will_complete_deletion(sess, autocommit_sess, wait_until):
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

    try:
        # wait for ttl
        wait_until(
            fetch=lambda: sess.execute(
                text("select extract(epoch from clock_timestamp() - created) from net._http_response where id = :request_id"),
                {"request_id": request_id},
            ).scalar(),
            predicate=lambda age: age is None or age >= 1.0,
            timeout=5,
            description=f"request {request_id} to exceed the ttl",
        )

        # Wake the worker manually, under normal operation this will happen when new requests are received
        sess.execute(text("select net.wake()"))
        sess.commit() # commit so worker  wakes

        # only one batch (batch_size=2) should be deleted by this wake
        (count,) = wait_until(
            fetch=lambda: sess.execute(text("select count(*) from net._http_response")).fetchone(),
            predicate=lambda r: r[0] <= 2,
            timeout=2,
            description="first batch of expired responses to be deleted",
        )
        assert count == 2

        # wait for another batch
        (count,) = wait_until(
            fetch=lambda: sess.execute(text("select count(*) from net._http_response")).fetchone(),
            predicate=lambda r: r[0] == 0,
            timeout=5,
            description="remaining batch of expired responses to be deleted",
        )
        assert count == 0
    finally:
        # Always restore ttl/batch_size, even on assertion failure - they're set
        # via ALTER SYSTEM so they would otherwise leak into later tests.
        autocommit_sess.execute(text("alter system reset pg_net.ttl"))
        autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
        autocommit_sess.execute(text("select pg_reload_conf();"))


def test_http_responses_will_delete_despite_restart(sess, autocommit_sess, wait_until):
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

    try:
        # wait for ttl to elapse and the worker to clean up all expired responses
        # (multiple worker cycles may be needed since batch_size=2 but there are 4 rows)
        (count,) = wait_until(
            fetch=lambda: sess.execute(text("select count(*) from net._http_response")).fetchone(),
            predicate=lambda r: r[0] == 0,
            timeout=20,
            description="all expired responses to be deleted after restart",
        )
        assert count == 0
    finally:
        # Always restore ttl/batch_size, even on assertion failure - they're set
        # via ALTER SYSTEM so they would otherwise leak into later tests.
        autocommit_sess.execute(text("alter system reset pg_net.ttl"))
        autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
        autocommit_sess.execute(text("select net.worker_restart()"))
        autocommit_sess.execute(text("select net.wait_until_running()"))
