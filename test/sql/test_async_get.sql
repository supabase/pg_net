begin;

    select http.async_get('http://supabase.io');

    select
        id, url
    from
        http.request_queue;

rollback
