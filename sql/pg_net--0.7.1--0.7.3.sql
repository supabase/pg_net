create or replace function net.check_worker_is_up() returns void as $$
begin
  if not exists (select pid from pg_stat_activity where backend_type ilike '%pg_net%') then
    raise exception using
      message = 'the pg_net background worker is not up'
    , detail  = 'the pg_net background worker is down due to an internal error and cannot process requests'
    , hint    = 'make sure that you didn''t modify any of pg_net internal tables';
  end if;
end
$$ language plpgsql;
comment on function net.check_worker_is_up() is 'raises an exception if the pg_net background worker is not up, otherwise it doesn''t return anything';
