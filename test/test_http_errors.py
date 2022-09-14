import time

import pytest
from sqlalchemy import text

def test_get_bad_url(sess):
    """net.http_get returns a descriptive errors for bad urls"""

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(
            """
            select net.http_get('localhost:8888');
        """
        )
    assert "URL using bad/illegal format or missing URL" in str(execinfo)

def test_bad_post(sess):
    """net.http_post with an empty url + body returns an error"""

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(
            """
            select net.http_post(null, '{"hello": "world"}');
        """
        )
    assert "violates not-null constraint" in str(execinfo)

def test_it_keeps_working_after_many_connection_refused(sess):
    """the worker doesn't crash on many failed responses with connection refused"""

    res = sess.execute(
        """
        select net.http_get('http://localhost:8888') from generate_series(1,10);
    """
    )
    sess.commit()

    (request_id,) = sess.execute(
        """
        select net.http_get('http://localhost:9999/p/200');
    """
    ).fetchone()

    sess.commit()

    response = sess.execute(
        text(
            """
        select * from net.http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert response[0] == "SUCCESS"
    assert response[1] == "ok"
    assert response[2].startswith("(200")

def test_it_keeps_working_after_server_returns_nothing(sess):
    """the worker doesn't crash on many failed responses with server returned nothing"""

    sess.execute(
        """
        select net.http_get('http://localhost:9999/p/200:d1') from generate_series(1,10);
    """
    )
    sess.commit()

    time.sleep(1.5)

    (error_msg,count) = sess.execute(
    """
        select error_msg, count(*) from net._http_response where status_code is null group by error_msg;
    """
    ).fetchone()

    assert error_msg == "Server returned nothing (no headers, no data)"
    assert count == 10

    sess.execute(
        """
        select net.http_get('http://localhost:9999/p/200:b"still_working"') from generate_series(1,10);
    """
    ).fetchone()

    sess.commit()

    time.sleep(1.5)

    (status_code,count) = sess.execute(
    """
        select status_code, count(*) from net._http_response where status_code = 200 group by status_code;
    """
    ).fetchone()

    assert status_code == 200
    assert count == 10
