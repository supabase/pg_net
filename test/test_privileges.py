from sqlalchemy import text
from common import collect_response_sync, http_request
import pytest
import psycopg


def test_net_on_postgres_role(sess):
    """Check that the postgres role can use the net schema by default"""

    role = sess.execute(text("select current_user;")).fetchone()
    assert role[0] == "postgres"

    request_id = http_request(
        sess,
        text(
            """
        select net.http_get(
            'http://localhost:8080/anything'
        );
    """
        ),
    )

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"


def test_net_on_pre_existing_role(conn):
    """Check that a pre existing role is not able to use net schema"""

    role = conn.execute("select current_user;").fetchall()
    assert role[0][0] == "postgres"

    conn.execute("set local role to pre_existing;")
    with pytest.raises(psycopg.errors.InsufficientPrivilege):
        conn.execute(
                """
            select net.http_get(
                'http://localhost:8080/anything'
            ), current_user;
        """
        )


def test_net_on_new_role(sess):
    """Check that a newly created role can use the net schema"""

    role = sess.execute(text("select current_user;")).fetchone()
    assert role[0] == "postgres"

    sess.execute(
        text("""
        create role another;
    """)
    )
    sess.execute(text("set local role to another;"))

    (request_id, current_user) = sess.execute(
        text(
            """
        select net.http_get(
            'http://localhost:8080/anything'
        ), current_user;
    """
        )
    ).fetchone()
    assert request_id == 1
    assert current_user == "another"

    # Commit to wakeup background worker
    sess.commit()

    # Confirm that the request was retrievable
    sess.execute(text("set local role to another;"))
    response = collect_response_sync(sess, request_id)
    current_user = sess.execute(text("select current_user;")).scalar()
    assert response["status"] == "SUCCESS"
    assert current_user == "another"

    sess.execute(text("set local role to another;"))
    # can use the net.worker_restart function
    (res, current_user) = sess.execute(
        text(
            """
        select net.worker_restart(), current_user;
    """
        )
    ).fetchone()
    assert res
    assert current_user == "another"

    sess.execute(
        text("""
        select net.wait_until_running();
        set local role postgres;
        drop role another;
    """)
    )
