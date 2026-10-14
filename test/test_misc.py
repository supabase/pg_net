def test_plpgsql_check(conn):
    sql = "create extension plpgsql_check;"
    conn.execute(sql)

    sql = """
    SELECT p.oid, p.proname, plpgsql_check_function(p.oid)
       FROM pg_catalog.pg_namespace n
       JOIN pg_catalog.pg_proc p ON pronamespace = n.oid
       JOIN pg_catalog.pg_language l ON p.prolang = l.oid
      WHERE l.lanname = 'plpgsql' AND p.prorettype <> 2279;
    """
    assert len(conn.execute(sql).fetchall()) == 0
