import time

import pytest
from sqlalchemy import text

wrong_port = 6666

def test_get_bad_url(sess):
    """net.http_get returns a descriptive errors for bad urls"""

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(text(
            f"""
            select net.http_get('localhost:{wrong_port}');
        """
        ))

    assert r"resolve proxy name" in str(execinfo)


def test_bad_post(sess):
    """net.http_post with an empty url + body returns an error"""

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(text(
            """
            select net.http_post(null, '{"hello": "world"}');
        """
        ))
    assert 'null value in column "url"' in str(execinfo)


def test_bad_get(sess):
    """net.http_get with an empty url + body returns an error"""

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(text(
            """
            select net.http_get(null);
        """
        ))
    assert 'null value in column "url"' in str(execinfo)


def test_bad_delete(sess):
    """net.http_delete with an empty url + body returns an error"""

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(text(
            """
            select net.http_delete(null);
        """
        ))
    assert 'null value in column "url"' in str(execinfo)


def test_bad_utils(sess):
    """util functions of pg_net return null"""

    res = sess.execute(text(
        """
        select net._encode_url_with_params_array(null, null);
    """
    )).scalar_one()

    assert res is None

    res = sess.execute(text(
        """
        select net._urlencode_string(null);
    """
    )).scalar_one()

    assert res is None


def test_it_keeps_working_after_many_connection_refused(sess):
    """the worker doesn't crash on many failed responses with connection refused"""

    res = sess.execute(text(
        f"""
        select net.http_get('http://localhost:{wrong_port}') from generate_series(1,10);
    """
    ))
    sess.commit()

    time.sleep(2)

    (error_msg,count) = sess.execute(text(
    """
        select error_msg, count(*) from net._http_response where status_code is null group by error_msg;
    """
    )).fetchone()

    # newer curl version changes the error message
    expected = ("Couldn't connect to server", "Could not connect to server")
    assert error_msg in expected
    assert count == 10

    (request_id,) = sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200');
    """
    )).fetchone()

    sess.commit()

    response = sess.execute(
        text(
            """
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert response[0] == "SUCCESS"
    assert response[1] == "ok"
    assert response[2].startswith("(200")

def test_it_keeps_working_after_server_returns_nothing(sess):
    """the worker doesn't crash on many failed responses with server returned nothing"""

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?disconnect=true') from generate_series(1,10);
    """
    ))
    sess.commit()

    time.sleep(1.5)

    (error_msg,count) = sess.execute(text(
    """
        select error_msg, count(*) from net._http_response where status_code is null group by error_msg;
    """
    )).fetchone()

    assert error_msg == "Server returned nothing (no headers, no data)"
    assert count == 10

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,10);
    """
    )).fetchone()

    sess.commit()

    time.sleep(1.5)

    (status_code,count) = sess.execute(text(
    """
        select status_code, count(*) from net._http_response where status_code = 200 group by status_code;
    """
    )).fetchone()

    assert status_code == 200
    assert count == 10
