-- Async
begin;

    with request as (
        select
            net.http_get('http://supabase.io', async:=true) as id
    )
    select
        net.http_collect_response(id)
    from
        request;

rollback;

/*
Pending worker fix
-- Sync
begin;

    with request as (
        select
            net.http_get('http://supabase.io', async:=false) as id
    )
    select
        net.http_collect_response(id)
    from
        request;

rollback;
*/
