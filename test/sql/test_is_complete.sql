begin;

    with req as (
        select http.async_get('http://supabase.io') as id
    )
    select
        http.is_complete(id)
    from
        req;

rollback;
