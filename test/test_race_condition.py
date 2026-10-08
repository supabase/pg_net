import threading
import time

import psycopg
import pytest

BACKEND_POINT = "pg-net-before-set-latch"
WORKER_POINT = "pg-net-worker-before-publish-latch"


def wait_for(cond, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if cond():
            return
        time.sleep(0.05)
    raise TimeoutError("condition not met")


def parked_at(conn, point):
    return conn.execute(
        "select count(*) from pg_stat_activity "
        "where wait_event_type = 'InjectionPoint' and wait_event = %s",
        (point,),
    ).fetchone()[0]


def test_repro_race(conn):
    try:
        conn.execute("create extension if not exists injection_points")
    except psycopg.Error:
        pytest.skip("injection_points is not available (needs PG17+ built with injection points)")
    conn.commit()
    conn.autocommit = True

    conn.execute("select injection_points_attach(%s, 'wait')", (BACKEND_POINT,))
    conn.execute("select injection_points_attach(%s, 'wait')", (WORKER_POINT,))

    a = psycopg.connect("dbname=postgres", autocommit=True)
    result = {}

    def restart():
        try:
            a.execute("select net.worker_restart()")
        except psycopg.OperationalError as e:
            result["crash"] = e

    t = threading.Thread(target=restart)
    t.start()

    # the backend has seen a non-NULL latch and is parked before SetLatch
    wait_for(lambda: parked_at(conn, BACKEND_POINT) == 1)
    # the worker exited (shared_latch = NULL), restarted and is parked before re-publishing it
    wait_for(lambda: parked_at(conn, WORKER_POINT) == 1)

    # release the backend: it re-reads the NULL latch and segfaults
    conn.execute("select injection_points_wakeup(%s)", (BACKEND_POINT,))
    t.join()

    assert "crash" in result
