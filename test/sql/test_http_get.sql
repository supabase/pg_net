-- Async
begin;

    with request as (
        select
            net.http_get('http://supabase.io') as id
    )
    select
        net.http_collect_response(id)
    from
        request;

rollback;

-- Sync
-- begin;
    -- with request as (
        -- select net.http_get('http://supabase.io', async:=false) as id
    -- )
    -- select
        -- net.http_collect_response(id)
    -- from
        -- request;
-- rollback;

-- TODO run these tests:
-- request with invalid protocol
-- select net.http_get('net://supabase.io');
-- request with invalid hostname
-- select net.http_get('http://new.ycombinator.com');
-- request with NULL body and content_type
-- select net.http_get('http://wikipedia.org');
