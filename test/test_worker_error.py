from sqlalchemy import text
import pytest
import time

def test_http_get_error_when_worker_down(sess):
    """net.http_get returns an error when pg background worker is down"""

    # kill the bg worker manually
    sess.execute(text("""
        select pg_terminate_backend((select pid from pg_stat_activity where backend_type = 'pg_net worker'));
    """))

    time.sleep(1);

    with pytest.raises(Exception) as execinfo:
        res = sess.execute(text(
            """
            select net.check_worker_is_up();
        """
        ))
    assert "the pg_net background worker is not up" in str(execinfo)
