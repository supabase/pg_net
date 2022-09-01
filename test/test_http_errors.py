import pytest

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
