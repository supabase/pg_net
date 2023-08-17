from sqlalchemy import text


def test_http_headers_set(sess):
    """Check that headers are being set"""
    # Create a request
    (request_id,) = sess.execute(
        """
        select net.http_get(
            url:='http://localhost:8080/headers',
            headers:='{"pytest-header": "pytest-header", "accept": "application/json"}'
        );
    """
    ).fetchone()

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
    print(response)
    assert response is not None
    assert response[0] == "SUCCESS"
    assert "pytest-header" in response[2]
