CREATE OR REPLACE FUNCTION net.http_collect_response(request_id 
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
  raise notice 'The net.http_collect_response function is deprecated.';
  select net._http_collect_response(request_id, async);
$$
