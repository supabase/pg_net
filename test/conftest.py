import time

import pytest
from sqlalchemy import create_engine
from sqlalchemy.orm import Session
from sqlalchemy import text

@pytest.fixture(scope="function")
def engine():
    engine = create_engine("postgresql:///postgres")
    yield engine
    engine.dispose()


@pytest.fixture(scope="function")
def sess(engine):

    session = Session(engine)

    # Reset sequences and tables between tests
    session.execute(text(
        """
    create extension if not exists pg_net;
    """
    ))
    session.commit()

    yield session

    session.rollback()

    session.execute(text(
        """
    drop extension if exists pg_net cascade;
    """
    ))
    session.commit()


@pytest.fixture(scope="function")
def autocommit_sess(engine):
    ac_engine = engine.execution_options(isolation_level="AUTOCOMMIT")
    session = Session(ac_engine)

    yield session


@pytest.fixture(scope="function")
def wait_until():
    """Poll `fetch()` until `predicate(result)` is true, instead of a fixed sleep.

    Returns the last fetched value on success, so callers can use it directly.
    Raises AssertionError with the last observed value if `timeout` is exceeded.
    """
    def _wait_until(fetch, predicate, timeout=10, interval=0.1, description="condition"):
        deadline = time.time() + timeout
        result = None
        while time.time() < deadline:
            result = fetch()
            if predicate(result):
                return result
            time.sleep(interval)
        raise AssertionError(
            f"Timed out after {timeout}s waiting for {description} (last value: {result!r})"
        )
    return _wait_until
