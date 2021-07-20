begin;

    with req as (
        select net.async_get('net://supabase.io') as id
    )
    select
        net.is_complete(id)
    from
        req;

rollback;
