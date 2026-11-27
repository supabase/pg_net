from sqlalchemy import text
from common import collect_response_sync, http_request


def test_upgrade_0_20_4_to_0_21_0(conn) -> None:
    sql = "DROP EXTENSION pg_net;"
    conn.execute(sql)
    sql = "CREATE EXTENSION pg_net WITH VERSION '0.20.4';"
    conn.execute(sql)

    sql = """
    insert into net.http_request_queue(method, url, headers, body, timeout_milliseconds)
    values (
        'DELETE'::pg_catalog.text,
        '',
        '{}'::pg_catalog.jsonb,
        '',
        0
    );
    """
    conn.execute(sql)

    sql = """
    insert into net._http_response(id)
    values (
        1
    );
    """
    conn.execute(sql)

    sql = "ALTER EXTENSION pg_net UPDATE;"
    conn.execute(sql)
    
    sql = "SELECT DISTINCT rolename FROM net._http_response;"
    results = conn.execute(sql).fetchall()
    assert len(results) == 1
    assert results[0][0] == 'public'

    sql = "SELECT DISTINCT rolename FROM net.http_request_queue;"
    conn.execute(sql)
    results = conn.execute(sql).fetchall()
    assert len(results) == 1
    assert results[0][0] == 'public'


def test_worker_serves_schema_without_rolename(sess) -> None:
    """The new library must work against a database still on the previous schema"""
    sess.execute(text("drop extension pg_net;"))
    sess.execute(text("create extension pg_net with version '0.20.4';"))
    sess.commit()

    request_id = http_request(sess, text("select net.http_get('http://localhost:8080/anything');"))
    response = collect_response_sync(sess, request_id)
    assert response["status"] == "SUCCESS"

    sess.execute(text("alter extension pg_net update;"))
    sess.commit()

    request_id = http_request(sess, text("select net.http_get('http://localhost:8080/anything');"))
    response = collect_response_sync(sess, request_id)
    assert response["status"] == "SUCCESS"
    (rolename,) = sess.execute(
        text("select rolename from net._http_response where id = :id"), {"id": request_id}
    ).fetchone()
    assert rolename == "public"
