import subprocess

import psycopg

OLD_VERSION = "0.20.6"


def privileges_of(conn, role):
    """What the role can do in net, through its own grants plus PUBLIC"""
    return conn.execute(
        """
        select
            has_schema_privilege(%(role)s, 'net', 'usage'),
            has_table_privilege(%(role)s, 'net.http_request_queue', 'select'),
            has_sequence_privilege(%(role)s, 'net.http_request_queue_id_seq', 'usage'),
            has_function_privilege(%(role)s, 'net.worker_restart()', 'execute');
        """,
        {"role": role},
    ).fetchone()


def test_upgrade_revokes_public(conn):
    """Upgrading from the last PUBLIC version takes everything away from roles that only had PUBLIC"""

    conn.execute("drop extension pg_net cascade;")
    conn.execute(f"create extension pg_net version '{OLD_VERSION}';")
    conn.commit()
    assert privileges_of(conn, "pre_existing") == (True, True, True, True)

    conn.execute("alter extension pg_net update;")
    conn.commit()
    (version,) = conn.execute(
        "select extversion from pg_extension where extname = 'pg_net';"
    ).fetchone()
    assert version != OLD_VERSION
    assert privileges_of(conn, "pre_existing") == (False, False, False, False)


def test_old_dump_restores_with_new_grants():
    """A dump taken on the last PUBLIC version restores on the new version, keeps the explicit grants and loses the PUBLIC ones"""

    admin = psycopg.connect("dbname=postgres", autocommit=True)
    admin.execute("drop database if exists dump_src;")
    admin.execute("drop database if exists dump_dst;")
    admin.execute("create database dump_src;")
    admin.execute("create database dump_dst;")
    try:
        with psycopg.connect("dbname=dump_src", autocommit=True) as src:
            src.execute(f"create extension pg_net version '{OLD_VERSION}';")
            src.execute("grant usage on schema net to pre_existing;")
            src.execute("grant select on net._http_response to pre_existing;")

        dump = subprocess.run(
            ["pg_dump", "--dbname=dump_src"], capture_output=True, check=True, text=True
        ).stdout
        subprocess.run(
            ["psql", "--dbname=dump_dst", "--set=ON_ERROR_STOP=1", "--quiet"],
            input=dump,
            capture_output=True,
            check=True,
            text=True,
        )

        with psycopg.connect("dbname=dump_dst") as dst:
            (version,) = dst.execute(
                "select extversion from pg_extension where extname = 'pg_net';"
            ).fetchone()
            assert version != OLD_VERSION
            assert privileges_of(dst, "pre_existing") == (True, False, False, False)
            assert dst.execute(
                "select has_table_privilege('pre_existing', 'net._http_response', 'select');"
            ).fetchone() == (True,)
    finally:
        admin.execute("drop database if exists dump_src;")
        admin.execute("drop database if exists dump_dst;")
        admin.close()
