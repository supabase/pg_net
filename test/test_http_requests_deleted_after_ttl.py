import time

import pytest
from sqlalchemy import text

def test_http_requests_deleted_after_ttl(sess):
    """Check that http requests are deleted within a few seconds of their ttl"""
    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_get(
            'https://httpbin.org/anything'
        );
    """
    ).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        select * from net.http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"

    # Sleep until after request should have been deleted
    time.sleep(3)

    # Ensure collecting the resposne now results in an error
    response = sess.execute(
        text(
            """
        select * from net.http_collect_response(:request_id);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    # TODO an ERROR status doesn't seem correct here
    assert response[0] == "ERROR"
