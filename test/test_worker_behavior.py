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

    autocommit_sess.execute(text("alter system set pg_net.batch_size to '1';"))
    autocommit_sess.execute(text("select net.worker_restart();"))
    autocommit_sess.execute(text("select net.wait_until_running();"))

    sess.execute(text(
        """
        select net.http_get('http://localhost:8080/pathological?status=200') from generate_series(1,5);
    """
    ))

    sess.commit()

    # truncate succeeds
    sess.execute(text(
        """
        truncate net.http_request_queue;
    """
    ))

    sess.commit()

    time.sleep(0.1)

    # and only one response will be done
    (count,) = sess.execute(text(
        """
        select count(*) from net._http_response;
    """
    )).fetchone()
    assert count == 1

    sess.commit()

    # even if some time passes
    time.sleep(1.1)

    (count,) = sess.execute(text(
        """
        select count(*) from net._http_response;
    """
    )).fetchone()
    assert count == 1

    sess.commit()

    # and the queue will be empty
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

