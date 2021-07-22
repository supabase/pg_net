begin;

    with req as (
        select net.http_get('https://supabase.io') as id
    )
    select
        net.http_cancel_request(id) is not null
    from
        req;

rollback;
