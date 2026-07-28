alter table net.http_request_queue
    add column use_callbacks bool not null default false,
    add column on_success text,
    add column on_error text,
    add column calling_role text;

-- Interface to make an async request handled by callbacks instead of the
-- net._http_response table. See https://github.com/supabase/pg_net/issues/62
-- API: Public
create or replace function net.http_request(
    -- http method (GET, POST or DELETE)
    method net.http_method,
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{}'::jsonb,
    -- optional body of the request
    body jsonb default null,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 5000,
    -- SQL command executed by the background worker when an HTTP response
    -- arrives (any status code). Parameters: $1 = status_code int,
    -- $2 = headers jsonb, $3 = body text. When null, the response is
    -- discarded without being stored anywhere (fire-and-forget).
    on_success text default null,
    -- SQL command executed by the background worker when the request fails
    -- without an HTTP response (timeout, connection error). Parameters:
    -- $1 = error message text, $2 = timed_out bool. When null, the failure
    -- is only reported in the database logs.
    on_error text default null
)
    returns void
    language plpgsql
as $$
declare
    params_array text[];
begin
    select coalesce(array_agg(net._urlencode_string(key) || '=' || net._urlencode_string(value)), '{}')
    into params_array
    from jsonb_each_text(params);

    -- Add to the request queue
    insert into net.http_request_queue(method, url, headers, body, timeout_milliseconds, use_callbacks, on_success, on_error, calling_role)
    values (
        method,
        net._encode_url_with_params_array(url, params_array),
        headers,
        convert_to(body::text, 'UTF8'),
        timeout_milliseconds,
        true,
        on_success,
        on_error,
        current_user
    );

    perform net.wake();
end
$$;
