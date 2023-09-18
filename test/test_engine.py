from sqlalchemy import text

def test_connect(sess):
    (x,) = sess.execute(text("select 1")).fetchone()
    assert x == 1
