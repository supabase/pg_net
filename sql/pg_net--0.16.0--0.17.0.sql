create or replace function net.wait_until_running()
  returns void
  language 'c'
as 'pg_net';
comment on function net.wait_until_running() is 'waits until the worker is running';
