ALTER FUNCTION net._encode_url_with_params_array (text, text[]) RETURNS NULL ON NULL INPUT;

ALTER TABLE net.http_request_queue DROP CONSTRAINT IF EXISTS http_request_queue_pkey CASCADE;

ALTER TABLE net._http_response DROP CONSTRAINT IF EXISTS _http_response_pkey CASCADE;

DROP TRIGGER IF EXISTS ensure_worker_is_up ON net.http_request_queue;

DROP FUNCTION IF EXISTS net._check_worker_is_up ();

CREATE OR REPLACE FUNCTION net.check_worker_is_up()
RETURNS void
AS $$
begin
  if not exists (select pid from pg_stat_activity where backend_type = 'pg_net worker') then
    raise exception using
      message = 'the pg_net background worker is not up'
    , detail  = 'the pg_net background worker is down due to an internal error and cannot process requests'
    , hint    = 'make sure that you didn''t modify any of pg_net internal tables';
  end if;
end
$$
LANGUAGE plpgsql;

DROP INDEX IF EXISTS net._http_response_created_idx;

ALTER TABLE net.http_request_queue SET UNLOGGED;

ALTER TABLE net._http_response SET UNLOGGED
