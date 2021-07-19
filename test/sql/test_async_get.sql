begin;

    select net.async_get('http://supabase.io');

    select
        id, url
    from
        net.request_queue;

rollback
