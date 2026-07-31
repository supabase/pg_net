import time
import pytest
from sqlalchemy import text
from common import http_requests


def skip_test_if_pg_is_old(sess):
    (pg_version,) = sess.execute(text(
        """
        select current_setting('server_version_num');
    """
    )).fetchone()

    if int(pg_version) < 140000:
        pytest.skip(
            "Skipping fixture on pg version < 14. The query_id column on pg_stat_statements is only available on >= 14")


def create_pg_stat_statement(sess):
    sess.execute(text(
        """
        create extension pg_stat_statements;
    """
    ))

    sess.commit()


def drop_pg_stat_statement(sess):
    sess.execute(text(
        """
        select pg_stat_statements_reset();
        drop extension pg_stat_statements;
    """
    ))

    sess.commit()


def get_worker_query_count(sess):
    (count,) = sess.execute(text(
        """
        select coalesce(sum(calls), 0)
        from pg_stat_statements
        where
            query ilike '%DELETE FROM net._http_response r %' or
            query ilike '%DELETE FROM net.http_request_queue%';
    """
    )).fetchone()

    return count


def test_query_stat_statements(sess):
    """
    Check that the background worker doesn't execute
    queries when no new requests arrive
    """

    skip_test_if_pg_is_old(sess)

    create_pg_stat_statement(sess)

    old_calls = get_worker_query_count(sess)

    # sleep for some time to see if new queries arrive
    time.sleep(3)

    new_calls = get_worker_query_count(sess)

    assert new_calls == old_calls

    drop_pg_stat_statement(sess)


def test_wakes_at_commit_time(sess):
    """
    Check that the background worker only does one wake at
    commit time, avoiding unnecessary wakes and work
    """

    skip_test_if_pg_is_old(sess)

    create_pg_stat_statement(sess)

    # wait for initial queries
    time.sleep(1)

    initial_calls = get_worker_query_count(sess)

    assert initial_calls >= 0

    http_requests(sess, text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,100);
    """
    ))

    # wait for reqs
    time.sleep(2)

    commit_calls = get_worker_query_count(sess)

    # only 4 queries should be made for the above requests
    # 2 queries at wake, 2 extra to check if there are more rows to be processed
    assert commit_calls == initial_calls + 4

    # if the new requests are rollbacked/aborted, then no new queries will be made by the bg worker
    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,100);
    """
    ))

    sess.rollback()

    # wait for requests
    time.sleep(2)

    rollback_calls = get_worker_query_count(sess)

    assert rollback_calls == commit_calls

    drop_pg_stat_statement(sess)
