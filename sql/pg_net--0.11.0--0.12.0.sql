ALTER FUNCTION net.http_get (text, jsonb, jsonb, integer) SECURITY INVOKER;

ALTER FUNCTION net.http_post (text, jsonb, jsonb, jsonb, integer) SECURITY INVOKER;

ALTER FUNCTION net.http_delete (text, jsonb, jsonb, integer) SECURITY INVOKER;

ALTER FUNCTION net._http_collect_response (bigint, boolean) SECURITY INVOKER;

ALTER FUNCTION net.http_collect_response (bigint, boolean) SECURITY INVOKER;

CREATE OR REPLACE FUNCTION net.worker_restart()
RETURNS bool
LANGUAGE c
AS $$pg_net$$;

GRANT USAGE
  ON SCHEMA net
  TO PUBLIC;

GRANT ALL PRIVILEGES
  ON ALL SEQUENCES IN SCHEMA net
  TO PUBLIC;

GRANT ALL PRIVILEGES
  ON ALL TABLES IN SCHEMA net
  TO PUBLIC
