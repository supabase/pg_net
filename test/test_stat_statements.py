import time

import pytest
from sqlalchemy import text

@pytest.mark.skip(reason="temporary disable")
def test_query_stat_statements(sess):
    """Check that the background worker executes queries despite no new requests arriving"""

    (pg_version,) = sess.execute(text(
        """
        select current_setting('server_version_num');
    """
    )).fetchone()

    if int(pg_version) < 140000:
        pytest.skip("Skipping fixture on pg version < 14. The query_id column on pg_stat_statements is only available on >= 14")

    sess.execute(text(
        """
        create extension pg_stat_statements;
    """
    ))

    sess.commit()

    time.sleep(1)

    (old_calls,) = sess.execute(text(
        """
        select
            pss.calls
        from
            pg_stat_activity sa
        join
            pg_stat_statements pss
            on sa.query_id = pss.queryid
        where
            sa.backend_type ilike '%pg_net%';
    """
    )).fetchone()

    time.sleep(3)

    (new_calls,) = sess.execute(text(
        """
        select
            pss.calls
        from
            pg_stat_activity sa
        join
            pg_stat_statements pss
            on sa.query_id = pss.queryid
        where
            sa.backend_type ilike '%pg_net%';
    """
    )).fetchone()

    assert new_calls > old_calls
