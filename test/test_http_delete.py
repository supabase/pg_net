from sqlalchemy import text

def test_http_delete_returns_id(sess):
    """net.http_delete returns a bigint id"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            url:='http://localhost:8080/delete'
        );
    """
    )).fetchone()

    assert request_id == 1


def test_http_delete_collect_sync_success(sess):
    """test net.http_delete works"""

    # Create a request
    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            url:='http://localhost:8080/delete'
        ,   params:= '{"param-foo": "bar"}'
        ,   headers:= '{"X-Baz": "foo"}'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Collect the response, waiting as needed
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
    assert response[1] == "ok"
    assert response[2] is not None
    assert "X-Baz" in response[2]
    assert "param-foo" in response[2]


def test_http_delete_positional_args(sess):
    """test net.http_delete works with positional arguments. This to ensure backwards compat when a new parameter is added to the function."""

    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Collect the response, waiting as needed
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
    assert response[1] == "ok"


    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Collect the response, waiting as needed
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
    assert response[1] == "ok"


    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}',
            '{"X-Baz": "foo"}'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Collect the response, waiting as needed
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
    assert response[1] == "ok"


    (request_id,) = sess.execute(text(
        """
        select net.http_delete(
            'http://localhost:8080/delete',
            '{"param-foo": "bar"}',
            '{"X-Baz": "foo"}',
            5000
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Collect the response, waiting as needed
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
    assert response[1] == "ok"
