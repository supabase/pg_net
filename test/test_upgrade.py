
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
