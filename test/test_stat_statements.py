import time
import pytest
from common import pg_wait_for_worker_state


def skip_test_if_pg_is_old(conn):
    (pg_version,) = conn.execute("select current_setting('server_version_num');").fetchone()

    if int(pg_version) < 140000:
        pytest.skip(
            "Skipping fixture on pg version < 14. The query_id column on pg_stat_statements is only available on >= 14"
        )


def create_pg_stat_statement(conn):
    conn.execute("create extension pg_stat_statements;")
    conn.commit()


def drop_pg_stat_statement(conn):
    conn.execute("select pg_stat_statements_reset();")
    conn.execute("drop extension pg_stat_statements;")
    conn.commit()


def get_worker_query_count(conn):
    (count,) = conn.execute(
        """
        select coalesce(sum(calls), 0)
        from pg_stat_statements
        where
            query ilike '%DELETE FROM net._http_response r %' or
            query ilike '%DELETE FROM net.http_request_queue%';
    """
    ).fetchone()
    conn.rollback()

    return count


def test_query_stat_statements(conn):
    """
    Check that the background worker doesn't execute
    queries when no new requests arrive
    """

    skip_test_if_pg_is_old(conn)

    create_pg_stat_statement(conn)

    # the worker runs one more cycle after the last request of the previous test, and its
    # queries would be counted below
    pg_wait_for_worker_state(conn, "idle")

    old_calls = get_worker_query_count(conn)

    # sleep for some time to see if new queries arrive
    time.sleep(3)

    new_calls = get_worker_query_count(conn)

    assert new_calls == old_calls

    drop_pg_stat_statement(conn)


def test_wakes_at_commit_time(conn):
    """
    Check that the background worker only does one wake at
    commit time, avoiding unnecessary wakes and work
    """

    skip_test_if_pg_is_old(conn)

    create_pg_stat_statement(conn)

    # wait for initial queries
    time.sleep(1)

    initial_calls = get_worker_query_count(conn)

    assert initial_calls >= 0

    conn.execute(
        "select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,100);"
    )
    conn.commit()

    # wait for reqs
    time.sleep(2)

    commit_calls = get_worker_query_count(conn)

    # only 4 queries should be made for the above requests
    # 2 queries at wake, 2 extra to check if there are more rows to be processed
    assert commit_calls == initial_calls + 4

    # if the new requests are rollbacked/aborted, then no new queries will be made by the bg worker
    conn.execute(
        "select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,100);"
    )
    conn.rollback()

    # wait for requests
    time.sleep(2)

    rollback_calls = get_worker_query_count(conn)

    assert rollback_calls == commit_calls

    drop_pg_stat_statement(conn)
