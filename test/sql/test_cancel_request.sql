begin;

    with req as (
        select http.async_get('http://supabase.io') as id
    )
    select
        http.cancel_request(id)
    from
        req;

    select
        *
    from
        http.request_queue;

rollback;
