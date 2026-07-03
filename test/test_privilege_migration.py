import time

import pytest
from sqlalchemy import text

# These tests simulate the migration path for fixing the trigger-based
# privilege escalation vulnerability: an unprivileged user could create a
# trigger on net.http_request_queue / net._http_response, which then runs
# with the privileges of whoever performs DML on those tables. Since the
# worker previously ran as the bootstrap superuser (when pg_net.username was
# unset), that trigger code executed as superuser.
#
# The fix is to configure pg_net.username to a non-superuser role. This
# suite checks that projects which already have data in the net tables
# (queued requests, historical responses) keep working after making that
# change - i.e. that Postgres' table-level GRANTs (not row "ownership")
# govern what the worker can do, so switching users doesn't orphan
# pre-existing rows.


def test_worker_switch_to_unprivileged_user_keeps_working_on_preexisting_data(sess, autocommit_sess):
    """After pointing pg_net.username at a non-superuser role, the worker must
    still be able to consume queue rows and expire response rows that were
    created before the switch (i.e. while it was still running as the
    bootstrap superuser)."""

    autocommit_sess.execute(text("drop role if exists net_worker;"))
    autocommit_sess.execute(text("create role net_worker nosuperuser login;"))

    try:
        # 1. Seed "pre-existing" data as it would exist on a project that's
        #    been running with the default (superuser) worker identity.

        # A request that's still pending in the queue. Direct inserts don't
        # call net.wake(), so it stays untouched until we explicitly wake
        # the worker below - simulating a row that was sitting there when
        # the GUC change takes effect.
        (request_id,) = sess.execute(text(
            """
            insert into net.http_request_queue(method, url, headers, timeout_milliseconds)
            values (
                'GET',
                net._encode_url_with_params_array('http://localhost:8080/pathological?status=200', '{}'),
                '{}',
                5000
            )
            returning id;
            """
        )).fetchone()

        # A historical response row, already old enough to be past any TTL,
        # standing in for data written while the worker still ran as the
        # bootstrap superuser.
        sess.execute(text(
            """
            insert into net._http_response
                (id, status_code, content_type, headers, content, timed_out, error_msg, created)
            values
                (999999, 200, 'text/plain', '{}', 'old response', false, null, now() - interval '1 hour');
            """
        ))

        sess.commit()

        (queue_count,) = sess.execute(text(
            "select count(*) from net.http_request_queue where id = :id;"
        ), {"id": request_id}).fetchone()
        assert queue_count == 1

        (resp_count,) = sess.execute(text(
            "select count(*) from net._http_response where id = 999999;"
        )).fetchone()
        assert resp_count == 1

        (worker_user,) = autocommit_sess.execute(text(
            "select usename from pg_stat_activity where backend_type ilike '%pg_net%';"
        )).fetchone()
        assert worker_user == 'postgres'

        # 2. Perform the migration: point the worker at a non-superuser role
        #    and lower the ttl so expiry of the old row happens quickly.
        autocommit_sess.execute(text("alter system set pg_net.username to 'net_worker';"))
        autocommit_sess.execute(text("alter system set pg_net.ttl to '1 second';"))
        autocommit_sess.execute(text("select net.worker_restart();"))
        autocommit_sess.execute(text("select net.wait_until_running();"))

        (worker_user,) = autocommit_sess.execute(text(
            "select usename from pg_stat_activity where backend_type ilike '%pg_net%';"
        )).fetchone()
        assert worker_user == 'net_worker'

        # 3. The pending request, created before the migration, must still
        #    be processed successfully - i.e. the unprivileged worker can
        #    SELECT/DELETE the queue row and INSERT the response.
        response = sess.execute(text(
            "select * from net._http_collect_response(:request_id, async:=false);"
        ), {"request_id": request_id}).fetchone()
        assert response[0] == "SUCCESS"

        (queue_count,) = sess.execute(text(
            "select count(*) from net.http_request_queue where id = :id;"
        ), {"id": request_id}).fetchone()
        assert queue_count == 0

        # 4. The pre-existing (already stale) response row must be deleted
        #    on the worker's next wake - i.e. the unprivileged worker can
        #    DELETE rows it did not itself insert.
        sess.execute(text("select net.wake();"))
        sess.commit()

        time.sleep(1)

        (resp_count,) = sess.execute(text(
            "select count(*) from net._http_response where id = 999999;"
        )).fetchone()

        assert resp_count == 0, (
            "the pre-existing response row was not cleaned up by the "
            "unprivileged worker - the account switch left orphaned rows "
            "the new role can't manage"
        )

    finally:
        autocommit_sess.execute(text("alter system reset pg_net.username;"))
        autocommit_sess.execute(text("alter system reset pg_net.ttl;"))
        autocommit_sess.execute(text("select net.worker_restart();"))
        autocommit_sess.execute(text("select net.wait_until_running();"))
        autocommit_sess.execute(text("drop role if exists net_worker;"))


def test_triggers_on_net_tables_run_as_pg_net_username(sess, autocommit_sess):
    """This is the actual vulnerability: a trigger planted on
    net.http_request_queue / net._http_response is a plpgsql function that
    defaults to SECURITY INVOKER, so it executes with the privileges of
    whoever performs the DML that fires it - not with the privileges of
    whoever created the trigger. Since the worker is the one deleting queue
    rows and inserting/deleting response rows, the trigger runs as whatever
    role the worker is connected as.

    Confirm both halves of that: with pg_net.username unset the trigger
    runs as the bootstrap superuser (the vulnerable default), and once
    pg_net.username is set to an unprivileged role the same trigger runs as
    that role instead.
    """

    (bootstrap_user,) = sess.execute(text("select current_user;")).fetchone()

    sess.execute(text(
        """
        create table public.trigger_witness(tbl text, who text, captured_at timestamptz default clock_timestamp());
        -- any role the worker connects as (including one created later in
        -- this test) must be able to record into the witness table
        grant insert on public.trigger_witness to public;

        create function public.record_trigger_user() returns trigger
            language plpgsql
            security invoker
        as $$
        begin
            insert into public.trigger_witness(tbl, who) values (TG_TABLE_NAME, current_user);
            return null;
        end;
        $$;

        -- fires on the worker's DELETE when it consumes a queued request
        create trigger witness_on_queue_delete
            after delete on net.http_request_queue
            for each row execute function public.record_trigger_user();

        -- fires on the worker's INSERT when it writes back a response
        create trigger witness_on_response_insert
            after insert on net._http_response
            for each row execute function public.record_trigger_user();
        """
    ))
    sess.commit()

    try:
        # Case 1: pg_net.username is unset -> worker runs as the bootstrap
        # superuser, so a malicious trigger would run with superuser
        # privileges. This is the vulnerable default.
        sess.execute(text(
            "select net.http_get('http://localhost:8080/pathological?status=200');"
        ))
        sess.commit()

        time.sleep(1)

        rows = sess.execute(text(
            "select tbl, who from public.trigger_witness order by captured_at;"
        )).fetchall()

        witnessed = {tbl: who for (tbl, who) in rows}
        assert witnessed.get('http_request_queue') == bootstrap_user, (
            f"expected the queue-delete trigger to run as the bootstrap "
            f"user {bootstrap_user!r}, but it ran as {witnessed.get('http_request_queue')!r}"
        )
        assert witnessed.get('_http_response') == bootstrap_user, (
            f"expected the response-insert trigger to run as the bootstrap "
            f"user {bootstrap_user!r}, but it ran as {witnessed.get('_http_response')!r}"
        )

        sess.execute(text("delete from public.trigger_witness;"))
        sess.commit()

        # Case 2: pg_net.username is configured to an unprivileged role ->
        # the same trigger must now run as that role, proving the fix
        # actually contains what a planted trigger can do.

        unprivileged_user = "net_worker"

        autocommit_sess.execute(text(f"drop role if exists {unprivileged_user};"))
        autocommit_sess.execute(text(f"create role {unprivileged_user} nosuperuser login;"))
        autocommit_sess.execute(text(f"alter system set pg_net.username to '{unprivileged_user}';"))
        autocommit_sess.execute(text("select net.worker_restart();"))
        autocommit_sess.execute(text("select net.wait_until_running();"))

        sess.execute(text(
            "select net.http_get('http://localhost:8080/pathological?status=200');"
        ))
        sess.commit()

        time.sleep(1)

        rows = sess.execute(text(
            "select tbl, who from public.trigger_witness order by captured_at;"
        )).fetchall()

        witnessed = {tbl: who for (tbl, who) in rows}
        assert witnessed.get('http_request_queue') == unprivileged_user, (
            f"expected the queue-delete trigger to run as {unprivileged_user!r}, "
            f"but it ran as {witnessed.get('http_request_queue')!r}"
        )
        assert witnessed.get('_http_response') == unprivileged_user, (
            f"expected the response-insert trigger to run as {unprivileged_user!r}, "
            f"but it ran as {witnessed.get('_http_response')!r}"
        )

    finally:
        # Drop the function (which cascades to the triggers on the net
        # tables) before dropping trigger_witness. The worker's own
        # transactions lock the net tables first and trigger_witness second
        # (queue/response DML, then the witness trigger's INSERT); dropping
        # trigger_witness first would acquire these same two locks in the
        # opposite order and can deadlock with an in-flight worker
        # transaction.
        sess.execute(text("drop function if exists public.record_trigger_user() cascade;"))
        sess.execute(text("drop table if exists public.trigger_witness;"))
        sess.commit()

        autocommit_sess.execute(text("alter system reset pg_net.username;"))
        autocommit_sess.execute(text("select net.worker_restart();"))
        autocommit_sess.execute(text("select net.wait_until_running();"))
        autocommit_sess.execute(text(f"drop role if exists {unprivileged_user};"))
