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
    """Check permissions on pre-existing role"""

    role = conn.execute("select current_user;").fetchall()
    assert role[0][0] == "postgres"

    conn.execute("set local role to pre_existing;")
    with pytest.raises(
        psycopg.errors.InsufficientPrivilege,
        match="permission denied for table http_request_queue",
    ):
        conn.execute(
            """
            select net.http_get(
                'http://localhost:8080/anything'
            ), current_user;
        """
        )

    conn.rollback()
    conn.execute("set local role to pre_existing;")
    conn.execute("DELETE FROM net.http_request_queue WHERE 1 = 0;")
    conn.execute("TRUNCATE net.http_request_queue;")


def test_net_on_new_role(conn):
    """Check permissions on newly created role"""

    role = conn.execute("select current_user;").fetchall()
    assert role[0][0] == "postgres"

    conn.execute("create role another;")
    conn.commit()
    conn.execute("set local role to another;")

    with pytest.raises(
        psycopg.errors.InsufficientPrivilege,
        match="permission denied for table http_request_queue",
    ):
        conn.execute(
            """
            select net.http_get(
                'http://localhost:8080/anything'
            ), current_user;
        """
        )

    conn.rollback()

    conn.execute("set local role to another;")
    conn.execute("DELETE FROM net.http_request_queue WHERE 1 = 0;")
    conn.execute("TRUNCATE net.http_request_queue;")

    conn.execute("set local role to another;")
    # can use the net.worker_restart function
    res = conn.execute("select net.worker_restart(), current_user;").fetchone()
    assert res[0]
    assert res[1] == "another"

    conn.execute("select net.wait_until_running();")

    conn.execute("set local role postgres;")
    conn.execute("drop role another;")
