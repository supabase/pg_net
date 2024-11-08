import time

import pytest
from sqlalchemy import text

def test_net_with_different_username_dbname(sess):
    """Check that a pre existing role can use the net schema"""

    # commit to avoid "cannot run inside a transaction block" error, see https://stackoverflow.com/a/75757326/4692662
    sess.execute(text("COMMIT"))
    sess.execute(text("alter system set pg_net.username to 'pre_existing'"))
    sess.execute(text("alter system set pg_net.database_name to 'pre_existing'"))
    sess.execute(text("select net.worker_restart()"))

    # bg worker restarts after 1 second
    time.sleep(1.1)

    ## can use the net.worker_restart function
    (username,datname) = sess.execute(
        text(
            """
        select usename, datname from pg_stat_activity where backend_type ilike '%pg_net%';
    """
        )
    ).fetchone()
    assert username == 'pre_existing'
    assert datname == 'pre_existing'

    sess.execute(text("COMMIT"))
    sess.execute(text("alter system reset pg_net.username"))
    sess.execute(text("select net.worker_restart()"))

    # bg worker restarts after 1 second
    time.sleep(1.1)


def test_net_appname(sess):
    """Check that pg_stat_activity has appname set"""

    ## can use the net.worker_restart function
    (count,) = sess.execute(
        text(
            """
        select count(1) from pg_stat_activity where application_name like '%pg_net%';
    """
        )
    ).fetchone()
    assert count == 1
