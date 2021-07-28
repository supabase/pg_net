import time

from sqlalchemy import text


def test_http_requests_deleted_after_ttl(sess):
    """Check that http requests are deleted within a few seconds of their ttl"""
    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_get(
            url:='https://httpbin.org/anything',
            ttl:='2 seconds'
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
    time.sleep(5)

    # Ensure collecting the resposne now results in an error
    response = sess.execute(
        text(
            """
        select * from net.http_collect_response(:request_id, async:=true);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "ERROR"
    assert "not found" in response[2]
