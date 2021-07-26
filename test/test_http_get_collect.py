from sqlalchemy import text


def test_http_get_returns_id(sess):
    """net.http_get returns a bigint id"""

    (request_id,) = sess.execute(
        """
        select net.http_get('https://news.ycombinator.com'); 
    """
    ).fetchone()

    assert request_id == 1


def test_collect_async_true_empty(sess):
    """Collect a non-existent response with async true"""

    # Collect the response, waiting as needed
    response = sess.execute(
        """
        select * from net.http_collect_response(1, async:=True); 
    """
    ).fetchone()

    assert response[0] is None


def test_http_get_collect_async_false(sess):
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
        select * from net.http_collect_response(:request_id, async:=False); 
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert response is not None
    assert response[0] == request_id
