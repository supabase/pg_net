create schema if not exists net;

-- Store pending requests. The background worker reads from here
-- API: Private
create table net.http_request_queue(
    id bigserial primary key,
    url text not null,
    params jsonb not null,
    headers jsonb not null,
    timeout_milliseconds int not null,
    -- Available for delete after this date
    delete_after timestamp not null
);
create index ix_http_request_queue_delete_after on net.http_request_queue (delete_after);


-- Associates a response with a request
-- API: Private
create table net.http_response(
    id bigint primary key references net.http_request_queue(id) on delete cascade,
    status_code integer,
    content_type text,
    headers jsonb,
    body text,
    timed_out bool
);


-- Blocks until an http_request is complete
-- API: Private
create or replace function net._await_response(
    request_id bigint
)
    returns bigint
    volatile
    parallel safe
    strict
    language plpgsql
as $$
declare
    rec net.http_response;
begin
    while rec is null loop
        select *
        into rec
        from net.http_response
        where id = request_id;

        if rec is null then
            -- Wait 50 ms before checking again
            perform pg_sleep(0.05);
        end if;
    end loop;

    return rec;
end;
$$;


-- Interface to make an async request
-- API: Public
create or replace function net.http_get(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb DEFAULT '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb DEFAULT '{}'::jsonb,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int DEFAULT 1000,
    -- the minimum amount of time the response should be persisted
    ttl interval default '3 days',
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- request_id reference
    returns bigint
    strict
    volatile
    parallel safe
    language plpgsql
as $$
declare
    request_id bigint;
    respone_rec net.http_response;
begin
    -- Add to the request queue
    insert into net.http_request_queue(url, params, headers, timeout_milliseconds, delete_after)
    values (url, params, headers, timeout_milliseconds, timezone('utc', now()) + ttl)
    returning id
    into request_id;

    -- If request is async, return id immediately
    if async then
        return request_id;
    end if;

    -- If sync, wait for the request to complete before returning
    perform net._await_http_response(request_id);
    return request_id;
end
$$;


-- Collect respones of an http request
-- API: Public
create or replace function net.http_collect_response(
    -- request_id reference
    request_id bigint,
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- http response composite type
    returns net.http_response
    strict
    volatile
    parallel safe
    language plpgsql
as $$
declare
    rec net.http_response;
begin

    if not async then
        perform net._await_http_response(request_id);
    end if;

    select *
    into rec
    from net.http_response
    where id = request_id;

    if rec is null then
        -- if rec is null it is a composite with every
        -- field nulled out rather than a single null value
        return null;
    end if;

    -- Return a valid, populated http_response
    return rec;
end;
$$;


-- Cancel a request in the queue
-- API: Public
create or replace function net.http_cancel_request(
    -- request_id reference
    request_id bigint
)
    -- request_id reference
    returns bigint
    strict
    volatile
    parallel safe
    language sql
as $$
    delete
    from net.http_request_queue
    where id = request_id
    returning id;
$$;
