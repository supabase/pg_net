create schema if not exists net;

create domain net.http_method as text
check (
    value ilike 'get' or
    value ilike 'post'
);

-- Store pending requests. The background worker reads from here
-- API: Private
create table net.http_request_queue(
    id bigserial primary key,
    method net.http_method not null,
    url text not null,
    headers jsonb not null,
    body bytea,
    -- TODO: respect this
    timeout_milliseconds int not null,
    created timestamptz not null default now()
);

create index created_idx on net.http_request_queue (created);

-- Associates a response with a request
-- API: Private
create table net._http_response(
    id bigint primary key references net.http_request_queue(id) on delete cascade,
    status_code integer,
    content_type text,
    headers jsonb,
    content text,
    timed_out bool,
    error_msg text
);

-- Blocks until an http_request is complete
-- API: Private
create or replace function net._await_response(
    request_id bigint
)
    returns bool
    volatile
    parallel safe
    strict
    language plpgsql
as $$
declare
    rec net._http_response;
begin
    while rec is null loop
        select *
        into rec
        from net._http_response
        where id = request_id;

        if rec is null then
            -- Wait 50 ms before checking again
            perform pg_sleep(0.05);
        end if;
    end loop;

    return true;
end;
$$;


-- url encode a string
-- API: Private
create or replace function net._urlencode_string(string varchar)
    -- url encoded string
    returns text

    language 'c'
    immutable
    strict
as 'pg_net';

-- API: Private
create or replace function net._encode_url_with_params_array(url text, params_array text[])
    -- url encoded string
    returns text

    language 'c'
    immutable
as 'pg_net';


-- Interface to make an async request
-- API: Public
create or replace function net.http_get(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{}'::jsonb,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 1000
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
    params_array text[];
begin
    select coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into params_array
    from jsonb_each_text(params);

    -- Add to the request queue
    insert into net.http_request_queue(method, url, headers, timeout_milliseconds)
    values (
        'GET',
        net._encode_url_with_params_array(url, params_array),
        headers,
        timeout_milliseconds
    )
    returning id
    into request_id;

    return request_id;
end
$$;

-- Interface to make an async request
-- API: Public
create or replace function net.http_post(
    -- url for the request
    url text,
    -- body of the POST request
    body jsonb default '{}'::jsonb,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{"Content-Type": "application/json"}'::jsonb,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int DEFAULT 1000
)
    -- request_id reference
    returns bigint
    volatile
    parallel safe
    language plpgsql
as $$
declare
    request_id bigint;
    params_array text[];
    content_type text;
begin

    -- Exctract the content_type from headers
    select
        header_value into content_type
    from
        jsonb_each_text(coalesce(headers, '{}'::jsonb)) r(header_name, header_value)
    where
        lower(header_name) = 'content-type'
    limit
        1;

    -- If the user provided new headers and omitted the content type
    -- add it back in automatically
    if content_type is null then
        select headers || '{"Content-Type": "application/json"}'::jsonb into headers;
    end if;

    -- Confirm that the content-type is set as "application/json"
    if content_type <> 'application/json' then
        raise exception 'Content-Type header must be "application/json"';
    end if;

    -- Confirm body is set since http method switches on if body exists
    if body is null then
        raise exception 'body must not be null';
    end if;

    select
        coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into
        params_array
    from
        jsonb_each_text(params);

    -- Add to the request queue
    insert into net.http_request_queue(method, url, headers, body, timeout_milliseconds)
    values (
        'POST',
        net._encode_url_with_params_array(url, params_array),
        headers,
        convert_to(body::text, 'UTF8'),
        timeout_milliseconds
    )
    returning id
    into request_id;

    return request_id;
end
$$;

-- Lifecycle states of a request (all protocols)
-- API: Public
create type net.request_status as enum ('PENDING', 'SUCCESS', 'ERROR');


-- A response from an HTTP server
-- API: Public
create type net.http_response AS (
    status_code integer,
    headers jsonb,
    body text
);

-- State wrapper around responses
-- API: Public
create type net.http_response_result as (
    status net.request_status,
    message text,
    response net.http_response
);


-- Collect respones of an http request
-- API: Public
create or replace function net.http_collect_response(
    -- request_id reference
    request_id bigint,
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- http response composite wrapped in a result type
    returns net.http_response_result
    strict
    volatile
    parallel safe
    language plpgsql
as $$
declare
    rec net._http_response;
    req_exists boolean;
begin

    if not async then
        perform net._await_response(request_id);
    end if;

    select *
    into rec
    from net._http_response
    where id = request_id;

    if rec is null then
        -- The request is either still processing or the request_id provided does not exist

        -- Check if a request exists with the given request_id
        select count(1) > 0
        into req_exists
        from net.http_request_queue
        where id = request_id;

        if req_exists then
            return (
                'PENDING',
                'request is pending processing',
                null
            )::net.http_response_result;
        end if;

        -- No request matching request_id found
        return (
            'ERROR',
            'request matching request_id not found',
            null
        )::net.http_response_result;

    end if;

    -- Return a valid, populated http_response_result
    return (
        'SUCCESS',
        'ok',
        (
            rec.status_code,
            rec.headers,
            rec.content
        )::net.http_response
    )::net.http_response_result;
end;
$$;

grant all on schema net to postgres;
grant all on all tables in schema net to postgres;
