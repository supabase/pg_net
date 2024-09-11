from sqlalchemy import text
import pytest
import time

def test_success_when_worker_is_up(sess):
    """net.check_worker_is_up should not return anything when the worker is running"""

    (result,) = sess.execute(text("""
        select net.check_worker_is_up();
    """)).fetchone()
    assert result is not None
    assert result == ''


def test_http_get_error_when_worker_down(sess):
    """net.http_get returns an error when pg background worker is down"""

    (restarted,) = sess.execute(text("""
        select net.worker_restart();
    """)).fetchone()
    assert restarted is not None
    assert restarted == True

    time.sleep(0.1)

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(text(
            """
            select net.check_worker_is_up();
        """
        ))
    assert "the pg_net background worker is not up" in str(execinfo)
