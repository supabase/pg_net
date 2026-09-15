def test_connect(conn):
    """Sanity test verifying connection to postgres works"""

    conn.execute("select 1")
    assert [(1, )] == conn.execute("select 1").fetchall()
