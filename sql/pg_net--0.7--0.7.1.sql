create or replace function net.http_collect_response(
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
  raise notice 'The net.http_collect_response function is deprecated.';
  select net._http_collect_response(request_id, async);
$$;
