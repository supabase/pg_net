alter function net._encode_url_with_params_array ( text, text[]) strict;

alter table net.http_request_queue drop constraint if exists http_request_queue_pkey cascade;
alter table net._http_response drop constraint if exists _http_response_pkey cascade;

drop trigger if exists ensure_worker_is_up on net.http_request_queue;
drop function if exists net._check_worker_is_up();

create or replace function net.check_worker_is_up() returns void as $$
begin
  if not exists (select pid from pg_stat_activity where backend_type = 'pg_net worker') then
    raise exception using
      message = 'the pg_net background worker is not up'
    , detail  = 'the pg_net background worker is down due to an internal error and cannot process requests'
    , hint    = 'make sure that you didn''t modify any of pg_net internal tables';
  end if;
end
$$ language plpgsql;

drop index if exists net._http_response_created_idx;

alter table net.http_request_queue set unlogged;
alter table net._http_response set unlogged;
