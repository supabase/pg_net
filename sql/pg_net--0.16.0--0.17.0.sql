CREATE OR REPLACE FUNCTION net.wait_until_running()
RETURNS void
LANGUAGE c
AS $$pg_net$$;

COMMENT ON FUNCTION net.wait_until_running ()
  IS 'waits until the worker is running'
