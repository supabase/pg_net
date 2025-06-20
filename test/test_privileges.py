import pytest
from sqlalchemy import text

def test_net_on_postgres_role(sess):
    """Check that the postgres role can use the net schema by default"""

    role = sess.execute(text("select current_user;")).fetchone()

    assert role[0] == "postgres"

    # Create a request
    (request_id,) = sess.execute(text(
        """
        select net.http_get(
            'http://localhost:8080/anything'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"


def test_net_on_pre_existing_role(sess):
    """Check that a pre existing role can use the net schema"""

    # Create a request
    (request_id,) = sess.execute(text(
        """
        set local role to pre_existing;
        select net.http_get(
            'http://localhost:8080/anything'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        set local role to pre_existing;
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"

    sess.execute(text("""
        set local role postgres;
    """))

def test_net_on_new_role(sess):
    """Check that a newly created role can use the net schema"""

    sess.execute(text("""
        create role another;
    """))

    # Create a request
    (request_id,) = sess.execute(text(
        """
        set local role to another;
        select net.http_get(
            'http://localhost:8080/anything'
        );
    """
    )).fetchone()

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        set local role to another;
        select * from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"

    ## can use the net.worker_restart function
    response = sess.execute(
        text(
            """
        set local role to another;
        select net.worker_restart();
    """
        )
    ).fetchone()
    assert response[0] == True

    sess.execute(text("""
        select net.wait_until_running();
        set local role postgres;
        drop role another;
    """))
