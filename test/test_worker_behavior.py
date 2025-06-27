from sqlalchemy import text
import sqlalchemy as sa
import pytest
import time

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

    # wait until it fails
    time.sleep(3)

    (up,) = sess.execute(text("""
        select is_worker_up();
    """)).fetchone()
    assert up is not None
    assert up == True
