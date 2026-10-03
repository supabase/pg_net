CREATE OR REPLACE FUNCTION net.check_worker_is_up()
RETURNS void
AS $$
begin
  if not exists (select pid from pg_stat_activity where backend_type ilike '%pg_net%') then
    raise exception using
      message = 'the pg_net background worker is not up'
    , detail  = 'the pg_net background worker is down due to an internal error and cannot process requests'
    , hint    = 'make sure that you didn''t modify any of pg_net internal tables';
  end if;
end
$$
LANGUAGE plpgsql;

COMMENT ON FUNCTION net.check_worker_is_up ()
  IS 'raises an exception if the pg_net background worker is not up, otherwise it doesn''t return anything'
