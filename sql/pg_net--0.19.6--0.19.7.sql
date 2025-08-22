create or replace function net._urlencode_string(string varchar)
    -- url encoded string
    returns text
    language 'c'
    immutable
as 'MODULE_PATHNAME';

create or replace function net._encode_url_with_params_array(url text, params_array text[])
    -- url encoded string
    returns text
    language 'c'
    immutable
as 'MODULE_PATHNAME';

create or replace function net.worker_restart()
  returns bool
  language 'c'
as 'MODULE_PATHNAME';

create or replace function net.wait_until_running()
  returns void
  language 'c'
as 'MODULE_PATHNAME';
comment on function net.wait_until_running() is 'waits until the worker is running';

create or replace function net.wake()
  returns void
  language 'c'
as 'MODULE_PATHNAME';
