CREATE OR REPLACE FUNCTION net.worker_restart()
RETURNS bool
AS $$
  select pg_reload_conf();
  select pg_terminate_backend(pid)
  from pg_stat_activity
  where backend_type ilike '%pg_net%';
$$ SECURITY DEFINER
LANGUAGE sql;

CREATE OR REPLACE FUNCTION net.http_get(url text
                                      , params jsonb = CAST('{}' AS jsonb)
                                      , headers jsonb = CAST('{}' AS jsonb)
                                      , timeout_milliseconds integer = 5000)
RETURNS bigint RETURNS NULL ON NULL INPUT VOLATILE PARALLEL safe
LANGUAGE plpgsql
AS $$
declare
    request_id bigint;
    params_array text[];
begin
    select coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into params_array
    from jsonb_each_text(params);

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

CREATE OR REPLACE FUNCTION net.http_post(url text
                                       , body jsonb = CAST('{}' AS jsonb)
                                       , params jsonb = CAST('{}' AS jsonb)
                                       , headers jsonb = CAST('{"Content-Type": "application/json"}' AS jsonb)
                                       , timeout_milliseconds integer = 5000)
RETURNS bigint VOLATILE PARALLEL safe
LANGUAGE plpgsql
AS $$
declare
    request_id bigint;
    params_array text[];
    content_type text;
begin

    select
        header_value into content_type
    from
        jsonb_each_text(coalesce(headers, '{}'::jsonb)) r(header_name, header_value)
    where
        lower(header_name) = 'content-type'
    limit
        1;

    if content_type is null then
        select headers || '{"Content-Type": "application/json"}'::jsonb into headers;
    end if;

    if content_type <> 'application/json' then
        raise exception 'Content-Type header must be "application/json"';
    end if;

    select
        coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into
        params_array
    from
        jsonb_each_text(params);

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

CREATE OR REPLACE FUNCTION net.http_delete(url text
                                         , params jsonb = CAST('{}' AS jsonb)
                                         , headers jsonb = CAST('{}' AS jsonb)
                                         , timeout_milliseconds integer = 5000)
RETURNS bigint RETURNS NULL ON NULL INPUT VOLATILE PARALLEL safe
LANGUAGE plpgsql
AS $$
declare
    request_id bigint;
    params_array text[];
begin
    select coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into params_array
    from jsonb_each_text(params);

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
$$
