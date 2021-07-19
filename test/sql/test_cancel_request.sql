begin;

    with req as (
        select net.async_get('net://supabase.io') as id
    )
    select
        net.cancel_request(id)
    from
        req;

    select
        *
    from
        net.request_queue;

rollback;
