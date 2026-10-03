CREATE OR REPLACE FUNCTION net._urlencode_string(string varchar)
RETURNS
-- url encoded string
text
LANGUAGE c IMMUTABLE
AS $$MODULE_PATHNAME$$;

CREATE OR REPLACE FUNCTION net._encode_url_with_params_array(url text
                                                           , params_array text[])
RETURNS
-- url encoded string
text
LANGUAGE c IMMUTABLE
AS $$MODULE_PATHNAME$$;

CREATE OR REPLACE FUNCTION net.worker_restart()
RETURNS bool
LANGUAGE c
AS $$MODULE_PATHNAME$$;

CREATE OR REPLACE FUNCTION net.wait_until_running()
RETURNS void
LANGUAGE c
AS $$MODULE_PATHNAME$$;

COMMENT ON FUNCTION net.wait_until_running ()
  IS 'waits until the worker is running';

CREATE OR REPLACE FUNCTION net.wake()
RETURNS void
LANGUAGE c
AS $$MODULE_PATHNAME$$
