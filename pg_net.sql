CREATE SCHEMA net;

ALTER SCHEMA net OWNER TO postgres;

CREATE DOMAIN net.http_method AS text CONSTRAINT http_method_check CHECK (value ~~* CAST('get' AS text)
                                                                       OR value ~~* CAST('post' AS text)
                                                                       OR value ~~* CAST('delete' AS text));

ALTER DOMAIN net.http_method OWNER TO postgres;

CREATE TYPE net.http_response AS (   status_code integer
                                  ,  headers jsonb
                                  ,  body text);

ALTER TYPE net.http_response OWNER TO postgres;

CREATE TYPE net.request_status AS ENUM ('PENDING'
                                      , 'SUCCESS'
                                      , 'ERROR');

ALTER TYPE net.request_status OWNER TO postgres;

CREATE TYPE net.http_response_result AS (   status net.request_status
                                         ,  message text
                                         ,  response net.http_response);

ALTER TYPE net.http_response_result OWNER TO postgres;

CREATE FUNCTION net._await_response(request_id bigint)
RETURNS boolean
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
            perform pg_sleep(0.05);
        end if;
    end loop;

    return true;
end;
$$;

ALTER FUNCTION net._await_response (request_id bigint) OWNER TO postgres;

CREATE FUNCTION net._encode_url_with_params_array(url text
                                                , params_array text[])
RETURNS text
LANGUAGE c IMMUTABLE AS '$libdir/pg_net'
                      , '_encode_url_with_params_array';

ALTER FUNCTION net._encode_url_with_params_array (url text, params_array text[]) OWNER TO postgres;

CREATE FUNCTION net._http_collect_response(request_id bigint
                                         , async boolean = TRUE)
RETURNS net.http_response_result
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


        return (
            'ERROR',
            coalesce(rec.error_msg, 'request matching request_id not found'),
            null
        )::net.http_response_result;

    end if;

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

ALTER FUNCTION net._http_collect_response (request_id bigint, async boolean) OWNER TO postgres;

CREATE FUNCTION net._urlencode_string(string varchar)
RETURNS text
LANGUAGE c IMMUTABLE AS '$libdir/pg_net'
                      , '_urlencode_string';

ALTER FUNCTION net._urlencode_string (string varchar) OWNER TO postgres;

CREATE FUNCTION net.check_worker_is_up()
RETURNS void
LANGUAGE plpgsql
AS $$
begin
  if not exists (select pid from pg_stat_activity where backend_type ilike '%pg_net%') then
    raise exception using
      message = 'the pg_net background worker is not up'
    , detail  = 'the pg_net background worker is down due to an internal error and cannot process requests'
    , hint    = 'make sure that you didn''t modify any of pg_net internal tables';
  end if;
end
$$;

ALTER FUNCTION net.check_worker_is_up () OWNER TO postgres;

COMMENT ON FUNCTION net.check_worker_is_up ()
  IS 'raises an exception if the pg_net background worker is not up, otherwise it doesn''t return anything';

CREATE FUNCTION net.http_collect_response(request_id bigint
                                        , async boolean = TRUE)
RETURNS net.http_response_result
LANGUAGE plpgsql
AS $$
begin;
  raise notice 'The net.http_collect_response function is deprecated.';
  select net._http_collect_response(request_id, async);
end;
$$;

ALTER FUNCTION net.http_collect_response (request_id bigint, async boolean) OWNER TO postgres;

CREATE FUNCTION net.http_delete(url text
                              , params jsonb = CAST('{}' AS jsonb)
                              , headers jsonb = CAST('{}' AS jsonb)
                              , timeout_milliseconds integer = 5000
                              , body jsonb = CAST(NULL AS jsonb))
RETURNS bigint
LANGUAGE plpgsql
AS $$
declare
    request_id bigint;
    params_array text[];
begin
    select coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into params_array
    from jsonb_each_text(params);

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

ALTER FUNCTION net.http_delete (url text, params jsonb, headers jsonb, timeout_milliseconds integer, body jsonb) OWNER TO postgres;

CREATE FUNCTION net.http_get(url text
                           , params jsonb = CAST('{}' AS jsonb)
                           , headers jsonb = CAST('{}' AS jsonb)
                           , timeout_milliseconds integer = 5000)
RETURNS bigint
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

    perform net.wake();

    return request_id;
end
$$;

ALTER FUNCTION net.http_get (url text, params jsonb, headers jsonb, timeout_milliseconds integer) OWNER TO postgres;

CREATE FUNCTION net.http_post(url text
                            , body jsonb = CAST('{}' AS jsonb)
                            , params jsonb = CAST('{}' AS jsonb)
                            , headers jsonb = CAST('{"Content-Type": "application/json"}' AS jsonb)
                            , timeout_milliseconds integer = 5000)
RETURNS bigint
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

    perform net.wake();

    return request_id;
end
$$;

ALTER FUNCTION net.http_post (url text, body jsonb, params jsonb, headers jsonb, timeout_milliseconds integer) OWNER TO postgres;

CREATE FUNCTION net.wait_until_running()
RETURNS void
LANGUAGE c AS '$libdir/pg_net'
            , 'wait_until_running';

ALTER FUNCTION net.wait_until_running () OWNER TO postgres;

COMMENT ON FUNCTION net.wait_until_running ()
  IS 'waits until the worker is running';

CREATE FUNCTION net.wake()
RETURNS void
LANGUAGE c AS '$libdir/pg_net'
            , 'wake';

ALTER FUNCTION net.wake () OWNER TO postgres;

CREATE FUNCTION net.worker_restart()
RETURNS boolean
LANGUAGE c AS '$libdir/pg_net'
            , 'worker_restart';

ALTER FUNCTION net.worker_restart () OWNER TO postgres;

CREATE UNLOGGED SEQUENCE net.http_request_queue_id_seq
  START WITH 1
  INCREMENT BY 1
  NO MINVALUE
  NO MAXVALUE
  CACHE 1;

ALTER SEQUENCE net.http_request_queue_id_seq OWNER TO postgres;

GRANT USAGE
  ON SCHEMA net
  TO PUBLIC;

GRANT ALL PRIVILEGES
  ON SEQUENCE net.http_request_queue_id_seq
  TO PUBLIC
