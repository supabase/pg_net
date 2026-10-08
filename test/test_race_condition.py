import time
import psycopg
import pytest


def test_repro_race():
    # The worker startup sleeps for 6 seconds. This waits for 8 seconds
    # to let the worker startup before proceeding with the test.
    time.sleep(8)
    a = psycopg.connect("dbname=postgres", autocommit=True)
    # worker_restart() reloads config, which makes the worker exit
    # while it sleeps before SetLatch
    with pytest.raises(psycopg.OperationalError):
        a.execute("select net.worker_restart()")
