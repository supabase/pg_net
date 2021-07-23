begin;

    with req as (
        select net.http_get('https://supabase.io', async:=true) as id
    )
    select
        net.http_collect_response(id, async:=true)
    from
        req;

rollback;
