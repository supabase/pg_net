from sqlalchemy import text

def test_http_header_missing_value(sess):
    """Check that a `MissingValue: ` header is processed correctly"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=missing-value'
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
    assert "MissingValue" in response[2]


def test_http_header_injection(sess):
    """Check that a `HeaderInjection Injected-Header: This header contains an injection` header fails without crashing"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=header-injection'
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
    assert response[0] == "ERROR"
    assert "Weird server reply" in response[1]


def test_http_header_spaces(sess):
    """Check that a `Spaces In Header Name: This header name contains spaces` header is processed correctly"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=spaces-in-header-name'
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
    assert "Spaces In Header Name" in response[2]


def test_http_header_non_printable_chars(sess):
    """Check that a `NonPrintableChars: NonPrintableChars\\u0001\\u0002` header is processed correctly"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=non-printable-chars'
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
    assert r"NonPrintableChars\\u0001\\u0002" in response[2]
