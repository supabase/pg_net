import pytest
import psycopg
import os
from sqlalchemy import create_engine
from sqlalchemy.orm import Session
from sqlalchemy import text
import psycopg
import filelock
from pathlib import Path
from tempfile import gettempdir
from contextlib import closing, contextmanager
import socket
import subprocess
import sys
import pathlib

from common import PSYCOPG_CONNSTR


def eprint(*args, **kwargs):
    """eprint prints to stderr"""

    print(*args, file=sys.stderr, **kwargs)


# this is out of ephemeral port range for many systems hence
# it is a lower change that it will conflict with "in-use" ports
PORT_LOWER_BOUND = 10200

# ephemeral port start on many Linux systems
PORT_UPPER_BOUND = 32768

next_port = PORT_LOWER_BOUND


@pytest.fixture(scope="function")
def engine(pg):
    engine = create_engine(PSYCOPG_CONNSTR.format(port=pg.port, host=pg.pgdata))
    yield engine
    engine.dispose()


@pytest.fixture(scope="function")
def conn(pg):
    """Direct connection via psycopg"""

    conn = psycopg.connect(
        f"dbname=postgres host={pg.pgdata} port={pg.port} user=postgres"
    )

    conn.execute("create extension if not exists pg_net;")
    conn.commit()

    yield conn

    conn.rollback()

    conn.execute("drop extension if exists pg_net cascade;")
    conn.commit()


@pytest.fixture(scope="function")
def sess(engine):
    session = Session(engine)

    # Reset sequences and tables between tests
    session.execute(
        text(
            """
    create extension if not exists pg_net;
    """
        )
    )
    session.commit()

    yield session

    session.rollback()

    session.execute(
        text(
            """
    drop extension if exists pg_net cascade;
    """
        )
    )
    session.commit()


@pytest.fixture(scope="function")
def autocommit_sess(engine):
    ac_engine = engine.execution_options(isolation_level="AUTOCOMMIT")
    session = Session(ac_engine)

    yield session


class PortLock:
    def __init__(self):
        global next_port
        while True:
            next_port += 1
            if next_port >= PORT_UPPER_BOUND:
                next_port = PORT_LOWER_BOUND

            self.lock = filelock.FileLock(Path(gettempdir()) / f"port-{next_port}.lock")
            try:
                self.lock.acquire(timeout=0)
            except filelock.Timeout:
                continue

            with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
                try:
                    s.bind(("127.0.0.1", next_port))
                    self.port = next_port
                    break
                except OSError:
                    continue

    def release(self):
        self.lock.release()


def run(command, *args, check=True, shell=None, silent=False, **kwargs):
    """run runs the given command and prints it to stderr"""

    if shell is None:
        shell = isinstance(command, str)

    if not shell:
        command = list(map(str, command))

    if not silent:
        if shell:
            eprint(f"+ {command}")
        else:
            # We could normally use shlex.join here, but it's not available in
            # Python 3.6 which we still like to support
            unsafe_string_cmd = " ".join(map(shlex.quote, command))
            eprint(f"+ {unsafe_string_cmd}")
    if silent:
        kwargs.setdefault("stdout", subprocess.DEVNULL)
    return subprocess.run(command, *args, check=check, shell=shell, **kwargs)


# TODO need to build test infra for ngnix server


def get_pg_version():
    with subprocess.Popen(
        ["pg_ctl", "--version"],
        stdout=subprocess.PIPE,
        text=True,
    ) as proc:
        version_line = proc.stdout.readlines()[0]
        if "beta" in version_line:
            pg_version = int(version_line.split(" ")[2].split("beta")[0])
        else:
            pg_version = int(version_line.split(" ")[2].split(".")[0].split("\n")[0])

    return pg_version


class Postgres:
    def __init__(self, pgdata):
        self.port_lock = PortLock()
        self.port = self.port_lock.port
        self.pgdata = pgdata
        self.log_path = self.pgdata / "pg.log"
        self.connections = {}
        self.cursors = {}
        self.restarted = False

    @property
    def conf_path(self):
        return self.pgdata / "postgresql.conf"

    def initdb(self):
        run(
            f"initdb -A trust --nosync --username postgres --pgdata {self.pgdata}",
            stdout=subprocess.DEVNULL,
        )
        with self.conf_path.open(mode="a") as pgconf:
            current_path = pathlib.Path(".").absolute()
            pgconf.write(f"dynamic_library_path='\\$libdir:{current_path}/build'\n")
            if get_pg_version() < 18:
                pgconf.write(
                    f"extension_control_path='\\$system:{current_path}/build/extension'\n"
                )
            else:
                pgconf.write(
                    f"extension_control_path='\\$system:{current_path}/build/'\n"
                )
            pgconf.write("shared_preload_libraries='pg_net,pg_stat_statements'")

    def pgctl(self, command, **kwargs):
        run(f"pg_ctl -w --pgdata {self.pgdata} {command}", **kwargs)

    def stop(self):
        self.pgctl("-m fast stop", check=False)

    def restart(self):
        self.restarted = True
        self.stop()
        self.start()

    def start(self):
        try:
            self.pgctl(f'-o "-k {self.pgdata} -p {self.port}" -l {self.log_path} start')
        except Exception:
            print("\n\nPG_LOG\n")
            with self.log_path.open() as f:
                print(f.read())
            raise

    def stop(self):
        self.pgctl("-m fast stop", check=False)

    def cleanup(self):
        self.stop()
        self.port_lock.release()


@pytest.fixture(autouse=True, scope="session")
def pg(tmp_path_factory):
    pg = Postgres(tmp_path_factory.getbasetemp() / "pgdata")
    pg.initdb()
    pg.start()

    sql = Path("test/init.sql").read_text()

    with psycopg.connect(
        f"postgresql://postgres@:{pg.port}/postgres?host={pg.pgdata}", autocommit=True
    ) as conn:
        with conn.cursor() as cur:
            cur.execute("create database pre_existing;")
            cur.execute("create role pre_existing nosuperuser login;")
            cur.execute(sql)

    yield pg
    pg.cleanup()


@pytest.fixture(autouse=True)
def pg_log(pg):
    """Prints the Postgres logs that were created during the test

    This can be useful for debugging a failure.
    """
    with pg.log_path.open() as f:
        f.seek(0, os.SEEK_END)
        yield
        print("\n\nPG_LOG\n")
        print(f.read())
