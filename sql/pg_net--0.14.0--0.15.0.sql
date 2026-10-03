DROP FUNCTION net.http_delete (text, jsonb, jsonb, integer);

CREATE FUNCTION net.http_delete(url 
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
integer = 5000
                              , body 
                              -- optional body of the request
jsonb = NULL)
RETURNS
-- request_id reference
bigint VOLATILE PARALLEL safe
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

    return request_id;
end
$$
