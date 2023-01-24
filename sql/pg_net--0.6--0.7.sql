alter domain net.http_method drop constraint http_method_check;
alter domain net.http_method add constraint http_method_check
check (
  value ilike 'get'
  or value ilike 'post'
  or value ilike 'delete'
);

drop function net.http_collect_response(bigint, boolean);

create or replace function net.http_delete(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{}'::jsonb,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 2000
)
    -- request_id reference
    returns bigint
    strict
    volatile
    parallel safe
    language plpgsql
    security definer
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
        'DELETE',
        net._encode_url_with_params_array(url, params_array),
        headers,
        timeout_milliseconds
    )
    returning id
    into request_id;

    return request_id;
end
$$;

create or replace function net._http_collect_response(
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
    security definer
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

        -- TODO: request in progress is indistinguishable from request that doesn't exist

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
