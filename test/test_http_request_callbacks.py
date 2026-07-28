import time

from sqlalchemy import text


def wait_until_queue_empty(sess, timeout=10):
    """Poll until the request queue is drained.

    The worker consumes the queue, runs the callbacks and commits in the same
    transaction, so once the queue is visibly empty the callbacks of the
    consumed batch are committed too.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        (count,) = sess.execute(text(
            """
            select count(*) from net.http_request_queue;
        """
        )).fetchone()
        sess.commit()
        if count == 0:
            return
        time.sleep(0.1)
    raise TimeoutError("request queue did not drain")


def test_http_request_fire_and_forget(sess):
    """without callbacks the response is discarded and nothing is stored"""

    sess.execute(text(
        """
        select net.http_request('GET', 'http://localhost:8080/pathological?status=200')
        from generate_series(1, 50);
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    (count,) = sess.execute(text(
        """
        select count(*) from net._http_response;
    """
    )).fetchone()

    assert count == 0

    (up,) = sess.execute(text(
        """
        select is_worker_up();
    """
    )).fetchone()

    assert up == True


def test_http_request_on_success(sess):
    """on_success receives $1 = status_code, $2 = headers, $3 = body"""

    sess.execute(text(
        """
        drop table if exists public.cb_results;
        create table public.cb_results(status int, headers jsonb, body text);
    """
    ))
    sess.commit()

    # the root path returns a non-empty body ("Hello world!")
    sess.execute(text(
        """
        select net.http_request(
            'GET',
            'http://localhost:8080/',
            on_success := 'insert into public.cb_results values ($1, $2, $3)'
        );
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    row = sess.execute(text(
        """
        select status, headers is not null, body from public.cb_results;
    """
    )).fetchone()

    assert row is not None
    assert row[0] == 200
    assert row[1] == True
    assert row[2] is not None
    assert "Hello world" in row[2]

    sess.execute(text("drop table public.cb_results;"))
    sess.commit()

    # nothing was stored in the response table
    (count,) = sess.execute(text(
        """
        select count(*) from net._http_response;
    """
    )).fetchone()

    assert count == 0


def test_http_request_on_success_empty_body(sess):
    """an empty response body reaches on_success as null"""

    sess.execute(text(
        """
        drop table if exists public.cb_empty;
        create table public.cb_empty(status int, body text);
    """
    ))
    sess.commit()

    # the pathological endpoint returns an empty body
    sess.execute(text(
        """
        select net.http_request(
            'GET',
            'http://localhost:8080/pathological?status=200',
            on_success := 'insert into public.cb_empty values ($1, $3)'
        );
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    row = sess.execute(text(
        """
        select status, body is null from public.cb_empty;
    """
    )).fetchone()

    assert row is not None
    assert row[0] == 200
    assert row[1] == True

    sess.execute(text("drop table public.cb_empty;"))
    sess.commit()


def test_http_request_on_success_gets_non_2xx_status(sess):
    """HTTP errors still go through on_success, with the status code"""

    sess.execute(text(
        """
        drop table if exists public.cb_statuses;
        create table public.cb_statuses(status int);
    """
    ))
    sess.commit()

    sess.execute(text(
        """
        select net.http_request(
            'GET',
            'http://localhost:8080/pathological?status=500',
            on_success := 'insert into public.cb_statuses values ($1)'
        );
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    (status,) = sess.execute(text(
        """
        select status from public.cb_statuses;
    """
    )).fetchone()

    assert status == 500

    sess.execute(text("drop table public.cb_statuses;"))
    sess.commit()


def test_http_request_post_with_body(sess):
    """POST requests work through the callback interface"""

    sess.execute(text(
        """
        drop table if exists public.cb_post;
        create table public.cb_post(status int);
    """
    ))
    sess.commit()

    sess.execute(text(
        """
        select net.http_request(
            'POST',
            'http://localhost:8080/pathological?status=200',
            headers := '{"Content-Type": "application/json"}'::jsonb,
            body := '{"hello": "world"}'::jsonb,
            on_success := 'insert into public.cb_post values ($1)'
        );
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    (status,) = sess.execute(text(
        """
        select status from public.cb_post;
    """
    )).fetchone()

    assert status == 200

    sess.execute(text("drop table public.cb_post;"))
    sess.commit()


def test_http_request_on_error(sess):
    """on_error receives $1 = error message, $2 = timed_out"""

    sess.execute(text(
        """
        drop table if exists public.cb_errors;
        create table public.cb_errors(error_msg text, timed_out bool);
    """
    ))
    sess.commit()

    # port 1 is closed, the request fails with a connection error
    sess.execute(text(
        """
        select net.http_request(
            'GET',
            'http://localhost:1',
            on_error := 'insert into public.cb_errors values ($1, $2)'
        );
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    row = sess.execute(text(
        """
        select error_msg, timed_out from public.cb_errors;
    """
    )).fetchone()

    assert row is not None
    assert len(row[0]) > 0
    assert row[1] == False

    sess.execute(text("drop table public.cb_errors;"))
    sess.commit()

    (up,) = sess.execute(text(
        """
        select is_worker_up();
    """
    )).fetchone()

    assert up == True


def test_http_request_failing_callback_does_not_kill_worker(sess):
    """a callback that raises doesn't abort the batch or crash the worker"""

    sess.execute(text(
        """
        select net.http_request(
            'GET',
            'http://localhost:8080/pathological?status=200',
            on_success := 'select 1/0'
        );
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    (up,) = sess.execute(text(
        """
        select is_worker_up();
    """
    )).fetchone()

    assert up == True

    # the worker keeps processing new requests afterwards
    (request_id,) = sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200');
    """
    )).fetchone()
    sess.commit()

    wait_until_queue_empty(sess)

    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert count == 1


def test_http_request_callback_runs_as_calling_role(sess):
    """callbacks are executed as the role that enqueued the request"""

    sess.execute(text(
        """
        drop table if exists public.cb_ident;
        create table public.cb_ident(who text);
        drop role if exists cb_limited;
        create role cb_limited;
        grant insert on public.cb_ident to cb_limited;
    """
    ))
    sess.commit()

    sess.execute(text(
        """
        set role cb_limited;
        select net.http_request(
            'GET',
            'http://localhost:8080/pathological?status=200',
            on_success := 'insert into public.cb_ident select current_user'
        );
        reset role;
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    (who,) = sess.execute(text(
        """
        select who from public.cb_ident;
    """
    )).fetchone()

    assert who == "cb_limited"

    sess.execute(text(
        """
        drop table public.cb_ident;
        drop role cb_limited;
    """
    ))
    sess.commit()


def test_http_request_callback_cannot_bypass_privileges(sess):
    """a callback enqueued by a low-privilege role can't write where the role can't"""

    sess.execute(text(
        """
        drop table if exists public.cb_secured;
        create table public.cb_secured(status int);
        drop role if exists cb_limited2;
        create role cb_limited2;
        -- note: no grant on cb_secured
    """
    ))
    sess.commit()

    sess.execute(text(
        """
        set role cb_limited2;
        select net.http_request(
            'GET',
            'http://localhost:8080/pathological?status=200',
            on_success := 'insert into public.cb_secured values ($1)'
        );
        reset role;
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    # the callback failed with permission denied, so no row was written
    (count,) = sess.execute(text(
        """
        select count(*) from public.cb_secured;
    """
    )).fetchone()

    assert count == 0

    (up,) = sess.execute(text(
        """
        select is_worker_up();
    """
    )).fetchone()

    assert up == True

    sess.execute(text(
        """
        drop table public.cb_secured;
        drop role cb_limited2;
    """
    ))
    sess.commit()


def test_legacy_functions_still_store_responses(sess):
    """net.http_get keeps the existing table-based behavior"""

    (request_id,) = sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200');
    """
    )).fetchone()
    sess.commit()

    wait_until_queue_empty(sess)

    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert count == 1


def test_callback_columns_missing_fallback(sess):
    """the worker keeps processing requests when the callback columns don't exist

    This simulates a binary that is newer than the installed extension version:
    the shared library is replaced on upgrade while `ALTER EXTENSION pg_net
    UPDATE` might not have been executed yet.
    """

    sess.execute(text(
        """
        alter table net.http_request_queue
            drop column use_callbacks,
            drop column on_success,
            drop column on_error,
            drop column calling_role;
    """
    ))
    sess.commit()

    # emulate the 0.20.4 net.http_get function, which doesn't reference the
    # callback columns (the 0.21.0 function can't run without the columns)
    (request_id,) = sess.execute(text(
        """
        insert into net.http_request_queue(method, url, headers, timeout_milliseconds)
        values ('GET', 'http://localhost:8080/pathological?status=200', '{}', 5000)
        returning id;
    """
    )).fetchone()

    sess.execute(text(
        """
        select net.wake();
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    # without the columns, the legacy behavior applies: the response is stored
    (count,) = sess.execute(
        text(
            """
        select count(*) from net._http_response where id = :request_id;
    """
        ),
        {"request_id": request_id},
    ).fetchone()

    assert count == 1

    # restore the columns, as `alter extension pg_net update` would
    sess.execute(text(
        """
        alter table net.http_request_queue
            add column use_callbacks bool not null default false,
            add column on_success text,
            add column on_error text,
            add column calling_role text;
    """
    ))
    sess.commit()

    sess.execute(text(
        """
        select net.http_request('GET', 'http://localhost:8080/pathological?status=200');
    """
    ))
    sess.commit()

    wait_until_queue_empty(sess)

    (up,) = sess.execute(text(
        """
        select is_worker_up();
    """
    )).fetchone()

    assert up == True
