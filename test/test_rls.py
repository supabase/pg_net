import psycopg
import pytest
from sqlalchemy import text
from common import collect_response_sync, http_request, wait_for_worker_state


def test_queue_table_insert_default(conn) -> None:
    sql = "CREATE USER test_user;"
    conn.execute(sql)

    sql = """
  insert into net.http_request_queue(method, url, headers, timeout_milliseconds)
    values (
        'GET',
        'http://localhost:8080',
        '{}'::jsonb,
        5000
    )
    returning id;
  """
    id = conn.execute(sql).fetchall()[0][0]
    conn.commit()
    sql = f"SELECT * FROM net.http_request_queue where id = {id};"
    response = conn.execute(sql).fetchall()
    assert len(response) == 1
    sql = "set role test_user;"
    response = conn.execute(sql)
    sql = f"SELECT * FROM net.http_request_queue where id = {id};"
    response = conn.execute(sql).fetchall()
    assert len(response) == 1
    sql = "RESET ROLE;"
    response = conn.execute(sql)
    sql = "DROP ROLE test_user;"
    conn.execute(sql)
    conn.commit()


def test_queue_table_insert_non_default(conn) -> None:
    sql = "CREATE role test_role;"
    conn.execute(sql)
    sql = "CREATE USER test_user;"
    conn.execute(sql)
    sql = "GRANT test_role to test_user;"
    conn.execute(sql)

    sql = """
  insert into net.http_request_queue(method, url, headers, timeout_milliseconds, rolename)
    values (
        'GET',
        'http://localhost:8080',
        '{}'::jsonb,
        5000,
        'test_role'
    )
    returning id;
  """
    id = conn.execute(sql).fetchall()[0][0]
    conn.commit()
    sql = f"SELECT * FROM net.http_request_queue where id = {id};"
    response = conn.execute(sql).fetchall()
    assert len(response) == 1
    sql = f"set role test_user;"
    conn.execute(sql)
    sql = f"SELECT * FROM net.http_request_queue where id = {id};"
    response = conn.execute(sql).fetchall()
    assert len(response) == 1

    sql = "RESET ROLE"
    conn.execute(sql)

    sql = "REVOKE test_role FROM test_user;"
    conn.execute(sql)

    sql = "set role test_user;"
    conn.execute(sql)

    sql = f"SELECT * FROM net.http_request_queue where id = {id};"
    response = conn.execute(sql).fetchall()
    assert len(response) == 0
    sql = "RESET ROLE;"
    conn.execute(sql)
    sql = "DROP ROLE test_user;"
    conn.execute(sql)
    sql = "DROP ROLE test_role;"
    conn.execute(sql)
    conn.commit()


def test_queue_table_rolename_null_rejected(conn) -> None:
    """A null rolename would stop the worker, so the table must refuse it"""
    with pytest.raises(psycopg.errors.NotNullViolation):
        conn.execute(
            """
            insert into net.http_request_queue(method, url, headers, timeout_milliseconds, rolename)
            values ('GET', 'http://localhost:8080', '{}'::jsonb, 5000, null);
            """
        )
    conn.rollback()


def test_response_table_rolename_copied_and_hidden(sess, autocommit_sess) -> None:
    """The worker copies rolename to the response and RLS hides it from non-members"""
    sess.execute(text("create role test_role;"))
    sess.execute(text("create user test_user;"))
    sess.commit()

    request_id = http_request(
        sess,
        text(
            """
            select net.http_get(
                url:='http://localhost:8080/anything',
                rolename:='test_role'
            );
            """
        ),
    )
    response = collect_response_sync(sess, request_id)
    assert response["status"] == "SUCCESS"

    (rolename,) = sess.execute(
        text("select rolename from net._http_response where id = :id"), {"id": request_id}
    ).fetchone()
    assert rolename == "test_role"

    sess.execute(text("set role test_user;"))
    assert (
        sess.execute(
            text("select count(*) from net._http_response where id = :id"), {"id": request_id}
        ).scalar()
        == 0
    )
    sess.execute(text("reset role;"))

    sess.execute(text("grant test_role to test_user;"))
    sess.execute(text("set role test_user;"))
    assert (
        sess.execute(
            text("select count(*) from net._http_response where id = :id"), {"id": request_id}
        ).scalar()
        == 1
    )
    sess.execute(text("reset role;"))

    sess.execute(text("drop role test_user;"))
    sess.execute(text("drop role test_role;"))
    sess.commit()
    # let the worker finish its trailing poll so the next test sees a quiet worker
    wait_for_worker_state(autocommit_sess, "idle")
