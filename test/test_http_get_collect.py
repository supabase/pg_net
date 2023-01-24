from sqlalchemy import text
import time


def test_http_get_returns_id(sess):
    """net.http_get returns a bigint id"""

    (request_id,) = sess.execute(
        """
        select net.http_get('https://news.ycombinator.com'); 
    """
    ).fetchone()

    assert request_id == 1


def test_http_get_collect_sync_success(sess):
    """Collect a response, waiting if it has not completed yet"""

    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_get('https://news.ycombinator.com');
    """
    ).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Collect the response, waiting as needed
    response = sess.execute(
        text(
            """
        select * from net.http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert response is not None
    assert response[0] == "SUCCESS"
    assert response[1] == "ok"
    assert response[2] is not None
    # psycopg2 does not deserialize nested composites
    assert response[2].startswith("(200")


# def test_http_get_collect_async_pending(sess):
#     """Collect a response async before completed"""

#     # Create a request
#     (request_id,) = sess.execute(
#         """
#         select net.http_get('https://news.ycombinator.com');
#     """
#     ).fetchone()

#     # Commit so background worker can start
#     sess.commit()

#     # Collect the response, waiting as needed
#     response = sess.execute(
#         text(
#             """
#         select * from net.http_collect_response(:request_id, async:=true);
#     """
#         ),
#         {"request_id": request_id},
#     ).fetchone()

#     assert response is not None
#     assert response[0] == "PENDING"
#     assert "pending" in response[1]
#     assert response[2] is None


def test_http_collect_response_async_does_not_exist(sess):
    """Collect a non-existent response with async true"""

    # Collect the response, waiting as needed
    response = sess.execute(
        """
        select * from net.http_collect_response(1, async:=true);
    """
    ).fetchone()

    assert response[0] == "ERROR"
    assert "not found" in response[1]
    assert response[2] is None

def test_http_get_responses_have_different_created_times(sess):
    """Ensure the rows in net._http_response have different created times"""

    sess.execute(
        """
        select net.http_get('http://localhost:8080/echo-method')
    """
    )
    sess.commit()

    time.sleep(1)

    sess.execute(
        """
        select net.http_get('http://localhost:8080/echo-method')
    """
    )
    sess.commit()

    time.sleep(1)

    sess.execute(
        """
        select net.http_get('http://localhost:8080/echo-method')
    """
    )
    sess.commit()

    time.sleep(1)

    count = sess.execute(
            """
        select count(distinct created) from net._http_response;
    """
    ).scalar()

    assert count == 3
