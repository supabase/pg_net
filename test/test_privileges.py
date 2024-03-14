import pytest
from sqlalchemy import text

def test_net_postgres_usage(sess):
    """Check that the postgres role can use the net schema by default"""

    role = sess.execute(text("select current_user;")).fetchone()

    assert role[0] == "postgres"

    # Create a request
    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            'http://localhost:8080/anything'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"

def test_net_another_usage(sess):
    """Check that another role can use the net schema when granted usage"""

    sess.execute(text("""
        set local role postgres;
        create role another;
        grant usage on schema net to another;
    """))

    # Create a request
    (request_id,) = sess.execute(text(
        """
        set local role to another;
        select net.http_get(
            'http://localhost:8080/anything'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        set local role to another;
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"

    sess.execute(text("""
        set local role postgres;
        drop owned by another;
        drop role another;
    """))

def test_net_fails_when_postgres_unprivileged(sess):
    """If the postgres role is unprivileged (no superuser + no privileges), the request will fail. This can happen on cloud databases where postgres is no superuser and the initial pg_net postgres privileges are revoked (can happen when upgrading)."""

    with pytest.raises(Exception) as execinfo:
        sess.execute(text("""
            set local role postgres;

            -- assume postgres is no superuser and has lost explicit privileges
            alter role postgres nosuperuser;
            revoke all on all tables in schema net from postgres;

            -- still, assume postgres has grant usage on the schema net
            grant usage on schema net to postgres;

            select net.http_get(
                'http://localhost:8080/anything'
            );
        """)).fetchone()
    assert "permission denied for table http_request_queue" in str(execinfo)
