DROP INDEX IF EXISTS created_idx;

ALTER TABLE net.http_request_queue DROP COLUMN created;

ALTER TABLE net._http_response DROP CONSTRAINT IF EXISTS _http_response_id_fkey;

ALTER TABLE net._http_response ADD COLUMN created timestamptz NOT NULL DEFAULT now();

CREATE INDEX 
  ON net._http_response (created);

CREATE OR REPLACE FUNCTION net.http_collect_response(request_id 
                                                   -- request_id reference
bigint
                                                   , async 
                                                   -- when `true`, return immediately. when `false` wait for the request to complete before returning
bool = TRUE)
RETURNS
-- http response composite wrapped in a result type
net.http_response_result RETURNS NULL ON NULL INPUT VOLATILE PARALLEL safe
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
