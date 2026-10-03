ALTER DOMAIN net.http_method DROP CONSTRAINT http_method_check;

ALTER DOMAIN net.http_method ADD CONSTRAINT http_method_check CHECK (value ILIKE 'get'
                                                                  OR value ILIKE 'post'
                                                                  OR value ILIKE 'delete');

DROP FUNCTION net.http_collect_response (bigint, boolean);

CREATE OR REPLACE FUNCTION net.http_delete(url 
                                         -- url for the request
text
                                         , params 
                                         -- key/value pairs to be url encoded and appended to the `url`
jsonb = CAST('{}' AS jsonb)
                                         , headers 
                                         -- key/values to be included in request headers
jsonb = CAST('{}' AS jsonb)
                                         , timeout_milliseconds 
                                         -- the maximum number of milliseconds the request may take before being cancelled
integer = 2000)
RETURNS
-- request_id reference
bigint RETURNS NULL ON NULL INPUT VOLATILE PARALLEL safe
LANGUAGE plpgsql SECURITY DEFINER
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

CREATE OR REPLACE FUNCTION net._http_collect_response(request_id 
                                                    -- request_id reference
bigint
                                                    , async 
                                                    -- when `true`, return immediately. when `false` wait for the request to complete before returning
bool = TRUE)
RETURNS
-- http response composite wrapped in a result type
net.http_response_result RETURNS NULL ON NULL INPUT VOLATILE PARALLEL safe
LANGUAGE plpgsql SECURITY DEFINER
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
$$
