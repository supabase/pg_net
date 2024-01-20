from sqlalchemy import text
import pytest
import time

def test_showing_worker_libcurl_version(sess):
    """the backend type shows the worker version and the libcurl version"""

    # wait for worker to start in case another test restarted it
    time.sleep(1)

    (result,) = sess.execute(
        """
        select backend_type like 'pg_net % worker using libcurl%' from pg_stat_activity where backend_type like '%pg_net%';
        """
    ).fetchone()
    assert result is True
