import time

import pytest
from sqlalchemy import text

def test_query_stat_statements(sess):
    """Check that the background worker doesn't execute queries when no new requests arrive"""

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
        select coalesce(sum(calls), 0)
        from pg_stat_statements
        where
            query ilike '%DELETE FROM net._http_response r %' or
            query ilike '%DELETE FROM net.http_request_queue%';
    """
    )).fetchone()

    # sleep for some time to see if new queries arrive
    time.sleep(3)

    (new_calls,) = sess.execute(text(
        """
        select coalesce(sum(calls), 0)
        from pg_stat_statements
        where
            query ilike '%DELETE FROM net._http_response r %' or
            query ilike '%DELETE FROM net.http_request_queue%';
    """
    )).fetchone()

    assert new_calls == old_calls

    sess.execute(text(
        """
        select pg_stat_statements_reset();
        drop extension pg_stat_statements;
    """
    ))

    sess.commit()


def test_wakes_at_commit_time(sess):
    """Check that the background worker only does one wake at commit time, avoiding unnecessary wakes and work"""

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

    # wait for initial queries
    time.sleep(1)

    (initial_calls,) = sess.execute(text(
        """
        select coalesce(sum(calls), 0)
        from pg_stat_statements
        where
            query ilike '%DELETE FROM net._http_response r %' or
            query ilike '%DELETE FROM net.http_request_queue%';
    """
    )).fetchone()

    assert initial_calls >= 0

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,100);
    """
    ))

    sess.commit()

    # wait for reqs
    time.sleep(2)

    (commit_calls,) = sess.execute(text(
        """
        select coalesce(sum(calls), 0)
        from pg_stat_statements
        where
            query ilike '%DELETE FROM net._http_response r %' or
            query ilike '%DELETE FROM net.http_request_queue%';
    """
    )).fetchone()

    assert commit_calls == initial_calls + 4 # only 4 queries should be made for the above requests
                                             # 2 queries at wake, 2 extra to check if there are more rows to be processed

    # if the new requests are rollbacked/aborted, then no new queries will be made by the bg worker
    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,100);
    """
    ))

    sess.rollback()

    # wait for requests
    time.sleep(2)

    (rollback_calls,) = sess.execute(text(
        """
        select coalesce(sum(calls), 0)
        from pg_stat_statements
        where
            query ilike '%DELETE FROM net._http_response r %' or
            query ilike '%DELETE FROM net.http_request_queue%';
    """
    )).fetchone()

    assert rollback_calls == commit_calls

    sess.execute(text(
        """
        select pg_stat_statements_reset();
        drop extension pg_stat_statements;
    """
    ))

    sess.commit()
