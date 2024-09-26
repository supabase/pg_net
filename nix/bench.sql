create view pg_net_stats as
select
  count(*) filter (where error_msg is null) as request_successes,
  count(*) filter (where error_msg is not null) as request_failures,
  (select error_msg from net._http_response where error_msg is not null order by id desc limit 1) as last_failure_error
from net._http_response;

create or replace procedure repro_timeouts(number_of_requests int default 10000, url text default 'http://server') as $$
declare
  last_id bigint;
  first_time timestamptz;
  second_time timestamptz;
  time_taken interval;
begin
  delete from net._http_response;

  with do_requests as (
    select
      net.http_get(url) as id
    from generate_series (1, number_of_requests) x
  )
  select id, clock_timestamp() into last_id, first_time from do_requests offset number_of_requests - 1;

  commit;

  raise notice 'Waiting until % requests complete', number_of_requests;

  perform net._await_response(last_id);

  select clock_timestamp() into second_time;

  select age(second_time, first_time) into time_taken;

  raise notice 'Stats: %', (select to_json(x) from pg_net_stats x limit 1);

  raise notice 'Time taken: %', time_taken;
end;
$$ language plpgsql;
