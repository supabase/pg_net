CREATE SCHEMA IF NOT EXISTS net;

CREATE DOMAIN net.http_method AS text CHECK (value ILIKE 'get'
                                          OR value ILIKE 'post'
                                          OR value ILIKE 'delete');

CREATE UNLOGGED TABLE
-- Store pending requests. The background worker reads from here
-- API: Private
net.http_request_queue (
    id bigserial
  , method net.http_method NOT NULL
  , url text NOT NULL
  , headers jsonb
  , body bytea
  , timeout_milliseconds integer NOT NULL
);

CREATE OR REPLACE FUNCTION net.check_worker_is_up()
RETURNS void
AS $$
begin
  if not exists (select pid from pg_stat_activity where backend_type ilike '%pg_net%') then
    raise exception using
      message = 'the pg_net background worker is not up'
    , detail  = 'the pg_net background worker is down due to an internal error and cannot process requests'
    , hint    = 'make sure that you didn''t modify any of pg_net internal tables';
  end if;
end
$$
LANGUAGE plpgsql;

COMMENT ON FUNCTION net.check_worker_is_up ()
  IS 'raises an exception if the pg_net background worker is not up, otherwise it doesn''t return anything';

CREATE UNLOGGED TABLE
-- Associates a response with a request
-- API: Private
net._http_response (
    id bigint
  , status_code integer
  , content_type text
  , headers jsonb
  , content text
  , timed_out bool
  , error_msg text
  , created timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX 
  ON net._http_response (created);

CREATE OR REPLACE FUNCTION net._await_response(request_id 
                                             -- Blocks until an http_request is complete
-- API: Private
bigint)
RETURNS bool
LANGUAGE plpgsql
AS $$
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

CREATE OR REPLACE FUNCTION net._urlencode_string(string 
                                               -- url encode a string
-- API: Private
varchar)
RETURNS
-- url encoded string
text
LANGUAGE c IMMUTABLE
AS $$MODULE_PATHNAME$$;

CREATE OR REPLACE FUNCTION net._encode_url_with_params_array(url 
                                                           -- API: Private
text
                                                           , params_array text[])
RETURNS
-- url encoded string
text
LANGUAGE c IMMUTABLE
AS $$MODULE_PATHNAME$$;

CREATE OR REPLACE FUNCTION net.worker_restart()
RETURNS bool
LANGUAGE c
AS $$MODULE_PATHNAME$$;

CREATE OR REPLACE FUNCTION net.wait_until_running()
RETURNS void
LANGUAGE c
AS $$MODULE_PATHNAME$$;

COMMENT ON FUNCTION net.wait_until_running ()
  IS 'waits until the worker is running';

CREATE OR REPLACE FUNCTION net.wake()
RETURNS void
LANGUAGE c
AS $$MODULE_PATHNAME$$;

CREATE OR REPLACE FUNCTION net.http_get(url 
                                      -- Interface to make an async request
-- API: Public
text
                                      , params 
                                      -- url for the request
jsonb = 
                                      -- key/value pairs to be url encoded and appended to the `url`
CAST('{}' AS jsonb)
                                      , headers 
                                      -- key/values to be included in request headers
jsonb = CAST('{}' AS jsonb)
                                      , timeout_milliseconds 
                                      -- the maximum number of milliseconds the request may take before being cancelled
integer = 5000)
RETURNS
-- request_id reference
bigint
LANGUAGE plpgsql
AS $$
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

    perform net.wake();

    return request_id;
end
$$;

CREATE OR REPLACE FUNCTION net.http_post(url 
                                       -- Interface to make an async request
-- API: Public
text
                                       , body 
                                       -- url for the request
jsonb = 
                                       -- body of the POST request
CAST('{}' AS jsonb)
                                       , params 
                                       -- key/value pairs to be url encoded and appended to the `url`
jsonb = CAST('{}' AS jsonb)
                                       , headers 
                                       -- key/values to be included in request headers
jsonb = CAST('{"Content-Type": "application/json"}' AS jsonb)
                                       , timeout_milliseconds 
                                       -- the maximum number of milliseconds the request may take before being cancelled
integer = 5000)
RETURNS
-- request_id reference
bigint
LANGUAGE plpgsql
AS $$
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

    perform net.wake();

    return request_id;
end
$$;

CREATE OR REPLACE FUNCTION net.http_delete(url 
                                         -- Interface to make an async request
-- API: Public
text
                                         , params 
                                         -- url for the request
jsonb = 
                                         -- key/value pairs to be url encoded and appended to the `url`
CAST('{}' AS jsonb)
                                         , headers 
                                         -- key/values to be included in request headers
jsonb = CAST('{}' AS jsonb)
                                         , timeout_milliseconds 
                                         -- the maximum number of milliseconds the request may take before being cancelled
integer = 5000
                                         , body 
                                         -- optional body of the request
jsonb = NULL)
RETURNS
-- request_id reference
bigint
LANGUAGE plpgsql
AS $$
declare
    request_id bigint;
    params_array text[];
begin
    select coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into params_array
    from jsonb_each_text(params);

    -- Add to the request queue
    insert into net.http_request_queue(method, url, headers, body, timeout_milliseconds)
    values (
        'DELETE',
        net._encode_url_with_params_array(url, params_array),
        headers,
        convert_to(body::text, 'UTF8'),
        timeout_milliseconds
    )
    returning id
    into request_id;

    perform net.wake();

    return request_id;
end
$$;

CREATE TYPE net.request_status AS ENUM ('PENDING'
                                      , 'SUCCESS'
                                      , 'ERROR');

CREATE TYPE 
-- Lifecycle states of a request (all protocols)
-- API: Public
net.http_response AS (   
                                  -- A response from an HTTP server
-- API: Public
status_code integer
                                  ,  headers jsonb
                                  ,  body text);

CREATE TYPE 
-- State wrapper around responses
-- API: Public
net.http_response_result AS (   status net.request_status
                                         ,  message text
                                         ,  response net.http_response);

CREATE OR REPLACE FUNCTION net._http_collect_response(request_id 
                                                    -- Collect respones of an http request
-- API: Private
bigint
                                                    , async 
                                                    -- request_id reference
bool = TRUE)
RETURNS
-- when `true`, return immediately. when `false` wait for the request to complete before returning
net.http_response_result
-- http response composite wrapped in a result type

LANGUAGE plpgsql
AS $$
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

CREATE OR REPLACE FUNCTION net.http_collect_response(request_id 
                                                   -- request_id reference
bigint
                                                   , async 
                                                   -- when `true`, return immediately. when `false` wait for the request to complete before returning
bool = TRUE)
RETURNS
-- http response composite wrapped in a result type
net.http_response_result
LANGUAGE plpgsql
AS $$
begin
  raise notice 'The net.http_collect_response function is deprecated.';
  select net._http_collect_response(request_id, async);
end;
$$;

GRANT USAGE
  ON SCHEMA net
  TO PUBLIC;

GRANT ALL PRIVILEGES
  ON ALL SEQUENCES IN SCHEMA net
  TO PUBLIC;

GRANT ALL PRIVILEGES
  ON ALL TABLES IN SCHEMA net
  TO PUBLIC
