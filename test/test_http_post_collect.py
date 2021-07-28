from sqlalchemy import text


def test_http_post_returns_id(sess):
    """net.http_post returns a bigint id"""

    (request_id,) = sess.execute(
        """
        select net.http_post(
            url:='https://httpbin.org/post',
            body:='{}'
        );
    """
    ).fetchone()

    assert request_id == 1


def test_http_post_collect_sync_success(sess):
    """Collect a response, waiting if it has not completed yet"""

    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_post(
            url:='https://httpbin.org/post',
            body:='{}'
        );
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


def test_http_post_collect_async_pending(sess):
    """Collect a response async before completed"""

    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_post(
            url:='https://httpbin.org/post',
            body:='{}'
        );
    """
    ).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Collect the response, waiting as needed
    response = sess.execute(
        text(
            """
        select * from net.http_collect_response(:request_id, async:=true);
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert response is not None
    assert response[0] == "PENDING"
    assert "pending" in response[1]
    assert response[2] is None


def test_http_post_collect_non_empty_body(sess):
    """Collect a response async before completed"""
    # Equivalent to
    # curl -X POST "https://httpbin.org/post" -H  "accept: application/json" -H  "Content-Type: application/json" -d "{\"hello\":\"world\"}"

    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_post(
            url:='https://httpbin.org/post',
            body:='{"hello": "world"}',
            headers:='{"content-type": "application/json", "accept": "application/json"}'
        );
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
    print(response)
    assert response is not None
    assert response[0] == "SUCCESS"
    assert "ok" in response[1]
    assert "json" in response[2]
    assert "hello" in response[2]
    assert "world" in response[2]
