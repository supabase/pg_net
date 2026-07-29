from sqlalchemy import text


def test_connect(sess):
    """
    Sanity test checking that connection to postgres works
    """

    (x,) = sess.execute(text("select 1")).fetchone()
    assert x == 1
