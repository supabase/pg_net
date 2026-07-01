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

    role = sess.execute(text("select current_user;")).fetchone()
    assert role[0] == "postgres"

    # Create a request
    (request_id, current_user) = sess.execute(text(
        """
        set local role to pre_existing;
        select net.http_get(
            'http://localhost:8080/anything'
        ), current_user;
    """
    )).fetchone()
    assert request_id == 1
    assert current_user == 'pre_existing'

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        set local role to pre_existing;
        select *, current_user from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"
    assert response[3] == 'pre_existing' # current-user

def test_net_on_new_role(sess):
    """Check that a newly created role can use the net schema"""

    role = sess.execute(text("select current_user;")).fetchone()
    assert role[0] == "postgres"

    sess.execute(text("""
        create role another;
    """))

    # Create a request
    (request_id, current_user) = sess.execute(text(
        """
        set local role to another;
        select net.http_get(
            'http://localhost:8080/anything'
        ), current_user;
    """
    )).fetchone()
    assert request_id == 1
    assert current_user == 'another'

    # Commit so background worker can start
    sess.commit()

    # Confirm that the request was retrievable
    response = sess.execute(
        text(
            """
        set local role to another;
        select *, current_user from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).fetchone()
    assert response[0] == "SUCCESS"
    assert response[3] == 'another' # current-user

    ## can use the net.worker_restart function
    (res, current_user) = sess.execute(
        text(
            """
        set local role to another;
        select net.worker_restart(), current_user;
    """
        )
    ).fetchone()
    assert res == True
    assert current_user == 'another'

    sess.execute(text("""
        select net.wait_until_running();
        set local role postgres;
        drop role another;
    """))


def test_public_roles_cannot_create_triggers_on_internal_tables(sess):
    """Internal tables must not allow trigger-based privilege escalation."""

    (can_select, can_trigger) = sess.execute(text("""
        select
            has_table_privilege('pre_existing', 'net._http_response', 'select'),
            has_table_privilege('pre_existing', 'net._http_response', 'trigger');
    """)).fetchone()

    assert can_select is True
    assert can_trigger is False

    sess.execute(text("""
        create table public.t1 (id text);

        create or replace function public.my_trigger_fn()
        returns trigger
        language plpgsql
        as $$
        begin
            insert into public.t1 (id) values (current_user);
            return new;
        end
        $$;
    """))

    with pytest.raises(Exception) as execinfo:
        sess.execute(text("""
            set local role to pre_existing;

            create trigger my_trigger
            after insert on net._http_response
            for each row
            execute function public.my_trigger_fn();
        """))

    assert "permission denied" in str(execinfo.value)
