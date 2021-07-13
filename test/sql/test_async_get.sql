begin;

    select http.async_get('http://supabase.io');

    select
        *
    from
        http.request_queue;

rollback
