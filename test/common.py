import time
from sqlalchemy import create_engine, text
from sqlalchemy.orm import Session


def http_request(sess, query):
    """
    Execute query and commit to wake up the background worker.

    The query should return a single row with a single column
    containing the request id of the request. Returns the request
    id
    """
    request_id = sess.execute(query).scalar_one()
    # Commit to wakeup background worker
    sess.commit()
    return request_id


def http_requests(sess, query):
    """
    Execute query and commit to wake up the background worker.

    The query usually contains multiple http requests. Returns
    the reqeust id of the first request in the query
    """
    (request_id,) = sess.execute(query).first()
    # Commit to wakeup background worker
    sess.commit()
    return request_id


def collect_response_sync(sess, request_id):
    """
    Wait for request with request_id to complete and return its response.

    Flattens net._http_collect_response's nested composite return type
    (status, message, response(status_code, headers, body)) into a single
    row, so callers get a name-addressable mapping (e.g. result["body"])
    instead of indexing into a stringified nested tuple.
    """
    return sess.execute(
        text(
            """
        select
            status,
            message,
            (response).status_code,
            (response).headers,
            (response).body
        from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).mappings().fetchone()


def is_worker_up(autocommit_sess):
    """
    Returns a function that checks whether worker is up or not

    The returned function captures autocommit_sess argument and
    uses it to track worker status. 
    """

    def fetch():
        (worker_is_up,) = autocommit_sess.execute(
            text("select is_worker_up();")
        ).fetchone()
        return worker_is_up
    return fetch


def get_queue_length(autocommit_sess):
    """
    Returns a function that returns the queue length

    The returned function captures autocommit_sess argument and
    uses it to track queue length. 
    """

    def fetch():
        (queue_length,) = autocommit_sess.execute(text("""
            select count(*) from net.http_request_queue;
        """)).fetchone()
        return queue_length
    return fetch


def get_response_count(autocommit_sess):
    """
    Returns a function that returns the number of rows in net._http_response table

    The returned function captures autocommit_sess argument and
    uses it to run the sql query. 
    """

    def fetch():
        (response_count,) = autocommit_sess.execute(text("""
            select count(*) from net._http_response;
        """)).fetchone()
        return response_count
    return fetch


def get_worker_state(autocommit_sess):
    """
    Returns a function that returns the background worker state

    The returned function captures autocommit_sess argument and
    uses it to run the sql query. 
    """

    def fetch():
        (state,) = autocommit_sess.execute(text("""
            select state from pg_stat_activity where backend_type ilike '%pg_net%';
        """)).fetchone()
        return state
    return fetch


def is_extension_installed(autocommit_sess):
    """
    Returns a function that returns whether pg_net is installed or not

    The returned function captures autocommit_sess argument and
    uses it to run the sql query. 
    """

    def fetch():
        (extension_installed,) = autocommit_sess.execute(text("""
            select count(*) = 1 from pg_extension where extname = 'pg_net';
        """)).fetchone()
        return extension_installed
    return fetch


def try_connect(engine, tmp_sess):
    """
    Returns a function that return whether postgres can accept connections.
    """

    def fetch():
        try:
            engine = create_engine("postgresql:///postgres")
            ac_engine = engine.execution_options(
                isolation_level="AUTOCOMMIT")
            tmp_sess = Session(ac_engine)
            return tmp_sess.execute(text("select 1")).fetchone()
        except Exception:
            return None
    return fetch


def wait_for_worker_down(autocommit_sess):
    """
    Waits until worker goes down

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        is_worker_up(autocommit_sess),
        lambda worker_is_up: not worker_is_up,
        description="background worker to go down",
    )


def wait_for_worker_up(autocommit_sess):
    """
    Waits until worker comes up

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        is_worker_up(autocommit_sess),
        lambda worker_is_up: worker_is_up,
        description="background worker to come up",
    )


def wait_for_worker_state(autocommit_sess, expected_state):
    """
    Waits until worker state matches expected_state

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        get_worker_state(autocommit_sess),
        lambda state: state == expected_state,
        description=f"background worker state to become {expected_state}",
    )


def wait_for_queue_drain(autocommit_sess):
    """
    Waits until the request queue is empty

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        get_queue_length(autocommit_sess),
        lambda queue_length: queue_length == 0,
        description="queue to drain"
    )


def wait_for_response_count(autocommit_sess, expected_count):
    """
    Waits until number of rows in net._http_response match expected_count

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        get_response_count(autocommit_sess),
        lambda response_count: response_count == expected_count,
        description="all responses to arrive"
    )


def wait_for_any_response(autocommit_sess):
    """
    Waits for at least one row in in net._http_response

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        get_response_count(autocommit_sess),
        lambda response_count: response_count > 0,
        description="any response to arrive"
    )


def wait_for_extension_drop(autocommit_sess):
    """
    Waits for pg_net to be dropped

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        is_extension_installed(autocommit_sess),
        lambda extension_installed: not extension_installed,
        description="extension to be dropped"
    )


def wait_for_postgres_ready(engine, tmp_sess):
    """
    Waits for postgres to be ready to accept connections

    Or throws an error if it doesn't within a timeout
    """

    wait_until(
        try_connect(engine, tmp_sess),
        lambda result: result is not None,
        description="postgres to become ready"
    )


def wait_until(fetch, predicate, timeout=10, sleep_interval=0.1, description="condition"):
    deadline = time.time() + timeout
    result = None
    while time.time() < deadline:
        result = fetch()
        if predicate(result):
            return result
        time.sleep(sleep_interval)
    raise AssertionError(
        f"Timed out after {timeout}s waiting for {description} (last value: {result!r})"
    )


def wakeup_worker(sess):
    """
    Wakes up the worker manually by calling net.wake() and committing
    """

    sess.execute(text("select net.wake()"))
    sess.commit()  # commit so worker  wakes


def restart_worker(sess):
    """
    Restarts the worker and waits for it to come back up

    You'd think that the following implementation should
    restart the worker and wait for it to come back up:

    sess.execute(text("select net.worker_restart()"))
    sess.execute(text("select net.wait_until_running()"))

    But it has a race condition in which this function might
    return before the worker has restarted. This happens because
    net.worker_restart() returns immediately after setting a flag
    to indicate to the core worker loop to restart. Then the
    net.wait_until_running() function waits for the worker state to
    become WS_RUNNING. But it can read the state from either the worker
    before the restart or after. In the first case it returns before
    the worker has restarted properly, and in the second case it
    behaves correctly.

    Instead we compare the pids of the workers before and after the
    restart which guarantees that the worker has restarted. After the
    restart we still run net.wait_until_running() for it to be
    intialized properly.
    """

    def fetch_worker_pid():
        return sess.execute(text("""
            select pid from pg_stat_activity where backend_type ilike '%pg_net%';
        """)).scalar()

    old_pid = fetch_worker_pid()
    sess.execute(text("select net.worker_restart()"))
    wait_until(
        fetch_worker_pid,
        lambda pid: pid is not None and pid != old_pid,
        description="background worker to restart with a new pid",
    )
    # the new worker's pg_stat_activity row appears slightly before it
    # publishes WS_RUNNING, so also wait for it to be fully up
    sess.execute(text("select net.wait_until_running()"))
