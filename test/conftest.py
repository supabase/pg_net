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

    # no autocommit
    session.connection().connection.set_isolation_level(0)

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
