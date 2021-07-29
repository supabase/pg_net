from sqlalchemy import text


def test_urlencode(sess):
    """test urlencoding a jsonb object"""

    (encoded_str,) = sess.execute(
        text(
            """
        select net._urlencode('{"key": "value", "other": "other value/"}'::jsonb)
    """
        )
    ).fetchone()

    assert encoded_str == "?key=value&other=other%20value%2F"


def test_http_get_url_params_set(sess):
    """Check that headers are being set

    Test url is (use browser):
    https://httpbin.org/anything?hello=world
    """
    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_get(
            url:='https://httpbin.org/anything',
            params:='{"hello": "world"}'::jsonb
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
    assert "?hello=world" in response[2]


def test_http_post_url_params_set(sess):
    """Check that headers are being set

    Test url is (use browser):
    https://httpbin.org/anything?hello=world
    """
    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_post(
            url:='https://httpbin.org/anything',
            params:='{"hello": "world"}'::jsonb
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
    assert "?hello=world" in response[2]
