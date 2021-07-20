begin;

    with req as (
        select net.async_get('net://supabase.io') as id
    )
    select
        net.cancel_request(id)
    from
        req;

    select
        id, url
    from
        net.request_queue;

rollback;
