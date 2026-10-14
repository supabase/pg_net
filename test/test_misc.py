def test_plpgsql_check(conn):
    """
    Test check all plpgsql functions (functions or trigger functions with defined 
    triggers) via plpgsql_check. Query sourced from below URL.

    https://github.com/okbob/plpgsql_check/blob/0ac96adc13b34d071c4be8ba0ec4f73e6e7520f5/README.md?plain=1#L310
    """
    sql = "create extension plpgsql_check;"
    conn.execute(sql)

    
    sql = """
    SELECT
        (pcf).functionid::regprocedure, (pcf).lineno, (pcf).statement,
        (pcf).sqlstate, (pcf).message, (pcf).detail, (pcf).hint, (pcf).level,
        (pcf)."position", (pcf).query, (pcf).context
    FROM
    (
        SELECT
            plpgsql_check_function_tb(pg_proc.oid, COALESCE(pg_trigger.tgrelid, 0),
                                      oldtable=>pg_trigger.tgoldtable,
                                      newtable=>pg_trigger.tgnewtable) AS pcf
        FROM pg_proc
        LEFT JOIN pg_trigger
            ON (pg_trigger.tgfoid = pg_proc.oid)
        WHERE
            prolang = (SELECT lang.oid FROM pg_language lang WHERE lang.lanname = 'plpgsql') AND
            pronamespace <> (SELECT nsp.oid FROM pg_namespace nsp WHERE nsp.nspname = 'pg_catalog') AND
            -- ignore unused triggers
            (pg_proc.prorettype <> (SELECT typ.oid FROM pg_type typ WHERE typ.typname = 'trigger') OR
             pg_trigger.tgfoid IS NOT NULL)
        OFFSET 0
    ) ss
    ORDER BY (pcf).functionid::regprocedure::text, (pcf).lineno;
    """
    assert len(conn.execute(sql).fetchall()) == 2
