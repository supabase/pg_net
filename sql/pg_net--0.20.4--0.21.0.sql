-- begin/end below was missed in below commit 
-- 8c059af7f86ea711c4b80d660bc37cd238c3b983
-- as apart of release 0.7.1
create or replace function net.http_collect_response(
    -- request_id reference
    request_id bigint,
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- http response composite wrapped in a result type
    returns net.http_response_result
    volatile
    language plpgsql
as $$
begin
  raise notice 'The net.http_collect_response function is deprecated.';
  return net._http_collect_response(request_id, async);
end;
$$;


-- coalesce below was missed in upgrade scripts in commit
-- 8b55e96d0d4b194dd8896b6f2a0fa8ef525aa8b3
-- as apart of release 0.7.3
create or replace function net._http_collect_response(
    -- request_id reference
    request_id bigint,
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- http response composite wrapped in a result type
    returns net.http_response_result
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

    if rec is null or rec.error_msg is not null then
        -- The request is either still processing or the request_id provided does not exist

        -- TODO: request in progress is indistinguishable from request that doesn't exist

        -- No request matching request_id found
        return (
            'ERROR',
            coalesce(rec.error_msg, 'request matching request_id not found'),
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

-- below alter tables corrects drift introduced in below commit
-- 07e63144c95748cef347e13abb5c5c3fa3b1e732
-- as apart of release v0.5
alter table net._http_response alter column id drop not null;

alter table net._http_response add column rolename text DEFAULT 'public';

alter table net.http_request_queue add column rolename text DEFAULT 'public';

alter table net.http_request_queue enable row level security;

alter table net._http_response enable row level security;

create policy net_request_policy
on net.http_request_queue
using (
       case
       when rolename OPERATOR(pg_catalog.=) 'public' then true
       else pg_catalog.pg_has_role(current_user, rolename, 'member')
       end
);

create policy net_response_policy
on net._http_response
using (
       case
       when rolename OPERATOR(pg_catalog.=) 'public' then true
       else pg_catalog.pg_has_role(current_user, rolename, 'member')
       end
);

drop function net.http_get(text, jsonb, jsonb, int);
drop function net.http_post(text, jsonb, jsonb, jsonb, int);
drop function net.http_delete(text, jsonb, jsonb, int, jsonb);

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
    timeout_milliseconds int default 5000,
    -- the role that should have access to the request/response data
    rolename text default 'public'
)
    -- request_id reference
    returns bigint
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
    insert into net.http_request_queue(method, url, headers, timeout_milliseconds, rolename)
    values (
        'GET',
        net._encode_url_with_params_array(url, params_array),
        headers,
        timeout_milliseconds,
	rolename
    )
    returning id
    into request_id;

    perform net.wake();

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
    timeout_milliseconds int DEFAULT 5000,
    -- the role that should have access to the request/response data
    rolename text default 'public'
)
    -- request_id reference
    returns bigint
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

    select
        coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into
        params_array
    from
        jsonb_each_text(params);

    -- Add to the request queue
    insert into net.http_request_queue(method, url, headers, body, timeout_milliseconds, rolename)
    values (
        'POST',
        net._encode_url_with_params_array(url, params_array),
        headers,
        convert_to(body::text, 'UTF8'),
        timeout_milliseconds,
	rolename
    )
    returning id
    into request_id;

    perform net.wake();

    return request_id;
end
$$;


create or replace function net.http_delete(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{}'::jsonb,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 5000,
    -- optional body of the request
    body jsonb default NULL,
    -- the role that should have access to the request/response data
    rolename text default 'public'
)
    -- request_id reference
    returns bigint
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
    insert into net.http_request_queue(method, url, headers, body, timeout_milliseconds, rolename)
    values (
        'DELETE',
        net._encode_url_with_params_array(url, params_array),
        headers,
        convert_to(body::text, 'UTF8'),
        timeout_milliseconds,
	rolename
    )
    returning id
    into request_id;

    perform net.wake();

    return request_id;
end
$$;
