from sqlalchemy import create_engine
from sqlalchemy.orm import Session
from sqlalchemy import text
import sqlalchemy as sa
import pytest
import time
import subprocess
import os

def test_worker_will_not_block_drop_database(autocommit_sess):
    """the worker will not block a session doing drop database"""

    autocommit_sess.execute(text("create database foo;"))
    autocommit_sess.execute(text("drop database foo;"))

    (result,) = autocommit_sess.execute(text("""
        select 1
    """)).fetchone()

    assert result is not None
    assert result == 1

    (pg_version,) = autocommit_sess.execute(text(
        """
        select current_setting('server_version_num');
    """
    )).fetchone()

    # drop database with force only available from pg 13
    if int(pg_version) >= 130000:

        autocommit_sess.execute(text("create database foo;"))
        autocommit_sess.execute(text("drop database foo with (force);"))

        (result,) = autocommit_sess.execute(text("""
            select 1
        """)).fetchone()

        assert result is not None
        assert result == 1


def test_success_when_worker_is_up(sess):
    """net.check_worker_is_up should not return anything when the worker is running"""

    (result,) = sess.execute(text("""
        select net.wait_until_running();
        select net.check_worker_is_up();
    """)).fetchone()
    assert result is not None
    assert result == ''


def test_worker_will_process_queue_when_up(sess):
    """when pg background worker is down and requests arrive, it will process them once it wakes up"""

    # check worker up
    (up,) = sess.execute(text("""
        select is_worker_up();
    """)).fetchone()
    assert up is not None
    assert up == True

    # restart it
    (restarted,) = sess.execute(text("""
        select public.kill_worker();
    """)).fetchone()
    assert restarted is not None
    assert restarted == True

    time.sleep(0.1)

    # check worker down
    (up,) = sess.execute(text("""
        select is_worker_up();
    """)).fetchone()
    assert up is not None
    assert up == False

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,10);
    """
    )).fetchone()

    sess.commit()

    # check requests where enqueued
    (count,) = sess.execute(text(
    """
        select count(*) from net.http_request_queue;
    """
    )).fetchone()

    assert count == 10

    # check worker is still down
    (up,) = sess.execute(text("""
        select is_worker_up();
    """)).fetchone()
    assert up is not None
    assert up == False

    sess.commit()

    # wait until up
    time.sleep(2.1)

    # check worker up
    (up,) = sess.execute(text("""
        select is_worker_up();
    """)).fetchone()
    assert up is not None
    assert up == True

    # wait until new requests are done
    time.sleep(1.1)

    (count,) = sess.execute(text(
    """
        select count(*) from net.http_request_queue;
    """
    )).fetchone()

    assert count == 0

    (status_code,count) = sess.execute(text(
    """
        select status_code, count(*) from net._http_response group by status_code;
    """
    )).fetchone()

    assert status_code == 200
    assert count == 10


def test_can_delete_rows_while_processing_queue(sess, autocommit_sess):
    """user can delete the queue rows while the worker is processing them"""

    autocommit_sess.execute(text("alter system set pg_net.batch_size to '1';"))
    autocommit_sess.execute(text("select net.worker_restart();"))
    autocommit_sess.execute(text("select net.wait_until_running();"))

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,10);
    """
    ))

    sess.commit()

    # leave time for some processing
    time.sleep(0.1)

    (count,) = sess.execute(text(
        """
        WITH deleted AS (DELETE FROM net.http_request_queue RETURNING *) SELECT count(*) FROM deleted;
    """
    )).fetchone()
    assert count > 1

    sess.commit()

    autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))


def test_truncate_wait_while_processing_queue(sess, autocommit_sess):
    """a truncate will not wait until the worker is done processing all requests"""

    # ensure the worker will be processing the queue 1 by 1 (slowly) so it doesn't clear the whole
    # net.http_request_queue in one go
    autocommit_sess.execute(text("alter system set pg_net.batch_size to '1';"))
    autocommit_sess.execute(text("select net.worker_restart();"))
    autocommit_sess.execute(text("select net.wait_until_running();"))

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,10);
    """
    ))
    sess.commit()

    # truncate succeeds fast, despite the worker still processing the queue 1 by 1
    sess.execute(text(
        """
        truncate net.http_request_queue;
    """
    ))

    # now the queue will be empty
    (count,) = sess.execute(text(
        """
        select count(*) from net.http_request_queue;
    """
    )).fetchone()
    assert count == 0

    autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))


def test_no_failure_on_drop_extension(sess):
    """while waiting for a slow request, a drop extension should wait and not crash the worker"""

    (request_id,) = sess.execute(text("""
        select net.http_get(url := 'http://localhost:8080/pathological?status=200&delay=2');
    """)).fetchone()
    assert request_id == 1

    sess.commit()

    # wait until processing
    time.sleep(1)

    sess.execute(text("""
        drop extension pg_net cascade;
    """))

    sess.commit()

    # wait until request is finished
    time.sleep(3)

    (up,) = sess.execute(text("""
        select is_worker_up();
    """)).fetchone()
    assert up is not None
    assert up == True


def test_worker_will_keep_processing_queue_when_restarted(sess, autocommit_sess):
    """when the background worker is restarted while working, it will pick up the remaining requests"""

    autocommit_sess.execute(text("alter system set pg_net.batch_size to '1';"))
    autocommit_sess.execute(text("select net.worker_restart();"))
    autocommit_sess.execute(text("select net.wait_until_running();"))

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,5);
    """
    ))

    sess.commit()

    # one restart will likely keep the worker awake since the wake signal could still be on, so do two restarts
    # to ensure the wake signal is cleared
    sess.execute(text(
        """
        select net.worker_restart();
        select net.wait_until_running();
    """
    ))

    time.sleep(0.1)

    sess.execute(text(
        """
        select net.worker_restart();
        select net.wait_until_running();
    """
    ))

    time.sleep(0.1)

    (status_code,count) = sess.execute(text(
    """
        select status_code, count(*) from net._http_response group by status_code;
    """
    )).fetchone()

    # at most 2 requests should have finished by now because of the low batch_size
    assert count <= 2
    assert count > 0 # at least 1 request should be finished
    assert status_code == 200

    # if we sleep for 4 seconds the whole 5 requests should be finished
    time.sleep(4)

    (status_code,count) = sess.execute(text(
    """
        select status_code, count(*) from net._http_response group by status_code;
    """
    )).fetchone()

    assert status_code == 200
    assert count == 5

    autocommit_sess.execute(text("alter system reset pg_net.batch_size"))
    autocommit_sess.execute(text("select net.worker_restart()"))
    autocommit_sess.execute(text("select net.wait_until_running()"))


def test_new_requests_get_attended_asap(sess):
    """new requests get attended as soon as possible"""

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,10);
    """
    ))

    sess.commit()

    # less than a second
    time.sleep(0.1)

    (status_code,count) = sess.execute(text(
    """
        select status_code, count(*) from net._http_response group by status_code;
    """
    )).fetchone()

    assert status_code == 200
    assert count == 10


def test_direct_inserts_no_requests(sess):
    """direct insertions to the net.http_request_queue doesn't trigger new requests"""

    sess.execute(text(
        """
        insert into net.http_request_queue(method, url, headers, timeout_milliseconds)
        values (
            'GET',
            net._encode_url_with_params_array('http://localhost:8080/pathological?status=200', '{}'),
            '{}',
            5000
        );
    """
    ))

    sess.commit()

    # wait for req
    time.sleep(0.1)

    # no response
    (count,) = sess.execute(text(
    """
        select count(*) from net._http_response;
    """
    )).fetchone()

    assert count == 0

    # req still in queue
    (count,) = sess.execute(text(
    """
        select count(*) from net.http_request_queue;
    """
    )).fetchone()

    assert count == 1

    # an explicit wake will make it serve requests though

    sess.execute(text(
    """
        select net.wake();
    """
    ))

    sess.commit()

    # wait for req
    time.sleep(0.1)

    (status_code, count) = sess.execute(text(
    """
        select status_code, count(*) from net._http_response group by status_code;
    """
    )).fetchone()

    assert status_code == 200
    assert count == 1


def test_processing_survives_postmaster_crash():
    """the queue will continue processing even when a postmaster crash or restart happens"""

    engine = create_engine("postgresql:///postgres")
    ac_engine = engine.execution_options(isolation_level="AUTOCOMMIT")
    tmp_sess = Session(ac_engine)

    tmp_sess.execute(text("create extension if not exists pg_net;"))

    tmp_sess.execute(text("alter system set pg_net.batch_size to '5';"))
    tmp_sess.execute(text("select net.worker_restart();"))
    tmp_sess.execute(text("select net.wait_until_running();"))

    tmp_sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,10);
    """
    )).fetchone()

    (count,) = tmp_sess.execute(text(
    """
        select count(*) from net.http_request_queue;
    """
    )).fetchone()
    assert count == 10

    engine.dispose()

    pgdata_env = os.getenv('PGDATA')
    subprocess.run(["pg_ctl", "restart", "-D", pgdata_env])

    # give it some time to finish restart
    time.sleep(1)

    engine = create_engine("postgresql:///postgres")
    ac_engine = engine.execution_options(isolation_level="AUTOCOMMIT")
    tmp_sess = Session(ac_engine)

    # give it enough time to finish processing the queue
    time.sleep(1)

    (count,) = tmp_sess.execute(text(
    """
        select count(*) from net.http_request_queue;
    """
    )).fetchone()
    assert count == 0

    (status_code,count) = tmp_sess.execute(text(
    """
        select status_code, count(*) from net._http_response group by status_code;
    """
    )).fetchone()

    assert status_code == 200
    assert count == 10

    tmp_sess.execute(text("alter system reset pg_net.batch_size"))
    tmp_sess.execute(text("select net.worker_restart()"))
    tmp_sess.execute(text("select net.wait_until_running()"))

    engine.dispose()


def test_worker_writes_increment_pgstat_counters(sess, autocommit_sess):
    """the worker's INSERTs into net._http_response must be reflected in
    pg_stat_user_tables. Without this, autovacuum/autoanalyze can never be
    scheduled and the table silently bloats.
    """

    # Make sure the worker is fully up before we start, otherwise we race
    # with whatever the previous test left behind.
    autocommit_sess.execute(text("select net.wait_until_running();"))

    # Clean baseline so deltas are unambiguous.
    autocommit_sess.execute(text(
        "select pg_stat_reset_single_table_counters('net._http_response'::regclass);"
    ))

    # Drive a batch of requests through the worker.
    sess.execute(text("""
        select net.http_get('http://localhost:8080/pathological?status=200')
        from generate_series(1,30);
    """))
    sess.commit()

    # Wait until the worker has actually drained the queue and written all
    # responses to net._http_response. Don't assume "30 rows" - the worker
    # may pick up the queue in chunks depending on wake() coalescing.
    for _ in range(20):
        time.sleep(0.5)
        (queue_count,) = sess.execute(text(
            "select count(*) from net.http_request_queue;"
        )).fetchone()
        if queue_count == 0:
            break

    # Confirm the worker actually wrote rows, otherwise the pgstat assertion
    # below would be meaningless.
    (resp_count,) = autocommit_sess.execute(text(
        "select count(*) from net._http_response;"
    )).fetchone()
    assert resp_count > 0, "worker did not write any responses"

    # Poll for the pgstat counter to reflect the worker's INSERTs. With
    # `pgstat_report_stat(false)` and PGSTAT_MIN_INTERVAL = 1000ms, the
    # worker's first flush attempt is normally a hit (last_flush is far in
    # the past after a long idle), but we allow generous slack here so an
    # off-by-a-tick scheduling doesn't flake the suite.
    deadline = time.time() + 30.0
    resp_ins = 0
    resp_mod = 0
    while time.time() < deadline:
        (resp_ins, resp_mod) = autocommit_sess.execute(text("""
            select n_tup_ins, n_mod_since_analyze
            from pg_stat_user_tables where relname='_http_response';
        """)).fetchone()
        if resp_ins > 0:
            break
        time.sleep(0.5)

    assert resp_ins > 0, (
        f"net._http_response.n_tup_ins is still 0 after 30s. "
        f"Worker wrote {resp_count} responses but none reached pgstat. "
        f"This is the pre-fix behaviour - autovacuum/autoanalyze will never "
        f"be scheduled and the table will silently bloat."
    )
    assert resp_mod > 0, (
        f"net._http_response.n_mod_since_analyze is still 0 after 30s "
        f"(n_tup_ins reported {resp_ins})."
    )


def test_worker_writes_trigger_autoanalyze_on_http_response(sess, autocommit_sess):
    """autoanalyze on net._http_response must fire after the worker writes
    enough rows. Without working pgstat counters, autovacuum/autoanalyze
    never get scheduled and the table bloats - this is the primary symptom
    seen on production (slow expiry DELETEs from a bloated index).
    """

    # Make sure the worker is fully up before we start.
    autocommit_sess.execute(text("select net.wait_until_running();"))

    # Make autovacuum eager *before* generating traffic so the launcher is
    # already running on a 1s naptime by the time stats threshold is crossed.
    # autovacuum_naptime is PGC_SIGHUP (reloadable). Give the reload a moment
    # to propagate to the launcher.
    autocommit_sess.execute(text("alter system set autovacuum_naptime = '1s';"))
    autocommit_sess.execute(text("select pg_reload_conf();"))
    time.sleep(1)

    # Per-table: trip the autoanalyze threshold after a handful of rows.
    # Reloptions take effect immediately; no reload required.
    autocommit_sess.execute(text("""
        alter table net._http_response set (
            autovacuum_analyze_threshold = 10,
            autovacuum_analyze_scale_factor = 0,
            autovacuum_vacuum_threshold = 10,
            autovacuum_vacuum_scale_factor = 0
        );
    """))

    autocommit_sess.execute(text(
        "select pg_stat_reset_single_table_counters('net._http_response'::regclass);"
    ))

    # Drive 30 inserts through the worker. 30 is well above the threshold (10).
    sess.execute(text("""
        select net.http_get('http://localhost:8080/pathological?status=200')
        from generate_series(1,30);
    """))
    sess.commit()

    # 30s budget covers worst-case worker pgstat flush (PGSTAT_MIN_INTERVAL
    # = 1s slack) + worst-case launcher cycle (autovacuum_max_workers=3,
    # 3 databases at 1s naptime each ~= 3s/cycle, with 2-3 cycle slack) +
    # autoanalyze worker spawn + ANALYZE on a tiny table (sub-second).
    # Real wall time on a clean rig is typically ~2-5s; the slack is to
    # absorb test-rig load and not flake.
    deadline = time.time() + 30.0
    autoanalyze_count = 0
    while time.time() < deadline:
        (autoanalyze_count,) = autocommit_sess.execute(text("""
            select autoanalyze_count
            from pg_stat_user_tables where relname='_http_response';
        """)).fetchone()
        if autoanalyze_count > 0:
            break
        time.sleep(0.5)

    assert autoanalyze_count > 0, (
        "autoanalyze never fired on net._http_response within 30s. "
        "Worker writes are not making pgstat threshold visible to the "
        "autovacuum launcher - the customer-facing symptom (silent bloat) "
        "would manifest in production."
    )

    # Cleanup: restore defaults so we don't bleed into other tests.
    autocommit_sess.execute(text("""
        alter table net._http_response reset (
            autovacuum_analyze_threshold,
            autovacuum_analyze_scale_factor,
            autovacuum_vacuum_threshold,
            autovacuum_vacuum_scale_factor
        );
    """))
    autocommit_sess.execute(text("alter system reset autovacuum_naptime;"))
    autocommit_sess.execute(text("select pg_reload_conf();"))


def test_worker_reports_activity_in_pg_stat_activity(sess, autocommit_sess):
    """the pg_net worker must call pgstat_report_activity() so its row in
    pg_stat_activity has a valid state column.
    """

    autocommit_sess.execute(text("select net.wait_until_running();"))

    # Wait for the worker to drain any leftover work from previous tests
    # and settle into idle. Polling makes this robust regardless of what
    # ran before.
    deadline = time.time() + 5.0
    state = None
    while time.time() < deadline:
        (state,) = autocommit_sess.execute(text(
            "select state from pg_stat_activity where backend_type ilike '%pg_net%';"
        )).fetchone()
        if state == 'idle':
            break
        time.sleep(0.1)
    assert state == 'idle', (
        f"pg_net worker state expected 'idle' at rest, got {state!r}. "
        "Without pgstat_report_activity(STATE_IDLE, ...) the state column "
        "stays NULL."
    )

    # Fire a slow request so the worker stays active long enough to observe.
    sess.execute(text("""
        select net.http_get('http://localhost:8080/pathological?status=200&delay=2');
    """))
    sess.commit()

    # Poll for 'active' for up to 5s. The slow request keeps the worker
    # busy for ~2s, so we have a wide observation window.
    deadline = time.time() + 5.0
    saw_active = False
    while time.time() < deadline:
        (state,) = autocommit_sess.execute(text(
            "select state from pg_stat_activity where backend_type ilike '%pg_net%';"
        )).fetchone()
        if state == 'active':
            saw_active = True
            break
        time.sleep(0.1)

    assert saw_active, (
        "pg_net worker state was never observed as 'active' during a slow "
        "request. Without pgstat_report_activity(STATE_RUNNING, ...) the "
        "state column stays NULL."
    )

