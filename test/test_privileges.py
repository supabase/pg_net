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


def test_net_grants_nothing_to_other_roles(conn):
    """A role that was not granted anything cannot use the net schema"""

    conn.execute("set local role to pre_existing;")

    with pytest.raises(
        psycopg.errors.InsufficientPrivilege,
        match="permission denied for schema net",
    ):
        conn.execute("select net.http_get('http://localhost:8080/anything');")
    conn.rollback()


def test_net_on_granted_role(conn):
    """A role with the grants from the README can send a request and read its response"""

    conn.execute("grant usage on schema net to pre_existing;")
    conn.execute("grant execute on function net.http_get to pre_existing;")
    conn.execute(
        "grant execute on function net.wake, net._urlencode_string, "
        "net._encode_url_with_params_array to pre_existing;"
    )
    conn.execute("grant insert, select (id) on net.http_request_queue to pre_existing;")
    conn.execute("grant usage on sequence net.http_request_queue_id_seq to pre_existing;")
    conn.execute("grant select on net._http_response to pre_existing;")
    conn.commit()

    try:
        conn.execute("set local role to pre_existing;")
        (request_id, current_user) = conn.execute(
            "select net.http_get('http://localhost:8080/anything'), current_user;"
        ).fetchone()
        assert current_user == "pre_existing"
        # Commit to wakeup background worker
        conn.commit()

        response = conn.execute(
            "select status from net._http_collect_response(%s, async := false);",
            (request_id,),
        ).fetchone()
        assert response[0] == "SUCCESS"

        conn.execute("set local role to pre_existing;")
        (status_code, current_user) = conn.execute(
            "select status_code, current_user from net._http_response where id = %s;",
            (request_id,),
        ).fetchone()
        assert status_code == 200
        assert current_user == "pre_existing"
        conn.rollback()

        # usage on the schema does not open the other functions
        conn.execute("set local role to pre_existing;")
        with pytest.raises(
            psycopg.errors.InsufficientPrivilege,
            match="permission denied for function worker_restart",
        ):
            conn.execute("select net.worker_restart();")
        conn.rollback()
    finally:
        conn.rollback()
        conn.execute("revoke all on schema net from pre_existing;")
        conn.execute("revoke all on all functions in schema net from pre_existing;")
        conn.execute("revoke all on all tables in schema net from pre_existing;")
        conn.execute("revoke all on all sequences in schema net from pre_existing;")
        conn.commit()
