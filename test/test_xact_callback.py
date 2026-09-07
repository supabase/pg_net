import pytest
from sqlalchemy import text


def test_wake_does_not_leak_xact_callbacks(sess, autocommit_sess):
    """
    net.wake() registers a transaction callback so the worker is woken at
    commit time. RegisterXactCallback appends a new entry on every call and
    never deduplicates, so it must be registered at most once per backend.
    Otherwise every transaction that calls net.http_* leaks an entry in
    TopMemoryContext for the lifetime of the connection.

    Measure TopMemoryContext of this backend before and after thousands of
    single-statement transactions that call net.wake(): it must stay flat.
    """
    (version_num,) = autocommit_sess.execute(
        text("show server_version_num")).fetchone()
    if int(version_num) < 140000:
        pytest.skip("pg_backend_memory_contexts requires PostgreSQL 14+")

    def top_memory_context_used_bytes():
        return autocommit_sess.execute(text("""
            select used_bytes
            from pg_backend_memory_contexts
            where name = 'TopMemoryContext'
        """)).scalar_one()

    iterations = 5000

    # warm up so one-off allocations (first callback registration, catalog
    # lookups, etc.) don't count towards the measurement
    for _ in range(50):
        autocommit_sess.execute(text("select net.wake()"))

    before = top_memory_context_used_bytes()

    # each execute is its own transaction under autocommit, so each one
    # runs wake() and then the commit callback
    for _ in range(iterations):
        autocommit_sess.execute(text("select net.wake()"))

    after = top_memory_context_used_bytes()
    growth = after - before
    print(f"TopMemoryContext growth over {iterations} net.wake() transactions: {growth} bytes")

    # a leaked callback entry costs 32 bytes (24-byte struct in a 32-byte
    # AllocSet chunk), so the buggy behavior grows by iterations * 32 bytes
    # (160 KB for 5000 iterations). Allow for some unrelated noise.
    assert growth < 32 * 1024, (
        f"TopMemoryContext grew by {growth} bytes over {iterations} "
        f"transactions calling net.wake(), expected it to stay flat"
    )
