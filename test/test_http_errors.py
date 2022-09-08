import pytest
from sqlalchemy import text

def test_http_get_bad_url(sess):
    """net.http_get returns a descriptive errors for bad urls"""

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(
            """
            select net.http_get('news.ycombinator.com');
        """
        )
    assert "URL using bad/illegal format or missing URL" in str(execinfo)

def test_http_get_bad_post(sess):
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
        select net.http_get('http://localhost:8888') from generate_series(1,100);
    """
    )
    sess.commit()

    (request_id,) = sess.execute(
        """
        select net.http_get('https://news.ycombinator.com');
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
