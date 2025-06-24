import time

import pytest
from sqlalchemy import text

def test_http_requests_deleted_after_ttl(sess):
    """Check that http requests will be deleted when they reach their ttl, not immediately but when the worker starts again"""

    # commit to avoid "cannot run inside a transaction block" error, see https://stackoverflow.com/a/75757326/4692662
    sess.execute(text("COMMIT"))
    sess.execute(text("alter system set pg_net.ttl to '4 seconds'"))
    sess.execute(text("select net.worker_restart()"))
    sess.execute(text("select net.wait_until_running()"))

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
    time.sleep(5)

    # Wake the worker manually, under normal operation this will happen when new requests are received
    sess.execute(text("select net.wake()"))

    # Ensure collecting the resposne now results in an error
    response = sess.execute(
        text(
            """
        select * from net._http_collect_response(:request_id);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "ERROR"

    sess.execute(text("COMMIT"))
    sess.execute(text("alter system reset pg_net.ttl"))
    sess.execute(text("select net.worker_restart()"))
    sess.execute(text("select net.wait_until_running()"))
