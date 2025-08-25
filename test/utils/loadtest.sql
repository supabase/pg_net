create table run (
  requests int,
  batch_size int,
  time_taken interval,
  request_successes bigint,
  request_failures bigint,
  last_failure_error text
);

-- loadtest using many gets, used to be called `repro_timeouts`
create or replace procedure wait_for_many_gets(number_of_requests int default 10000, url text default 'http://localhost:8080') as $$
declare
  last_id bigint;
  first_time timestamptz;
  second_time timestamptz;

  request_successes bigint;
  request_failures bigint;
  last_failure_error text;
begin
  delete from net._http_response;

  with do_requests as (
    select
      net.http_get(url) as id
    from generate_series (1, number_of_requests) x
  )
  select id, clock_timestamp() into last_id, first_time from do_requests offset number_of_requests - 1;

  commit;

  raise notice 'Waiting until % requests complete, using a pg_net.batch_size of %', number_of_requests, current_setting('pg_net.batch_size')::text;

  perform net._await_response(last_id);

  select clock_timestamp() into second_time;

  select
    count(*) filter (where error_msg is null),
    count(*) filter (where error_msg is not null),
    (select error_msg from net._http_response where error_msg is not null order by id desc limit 1)
  into request_successes, request_failures, last_failure_error
  from net._http_response;

  insert into run values (
    number_of_requests, current_setting('pg_net.batch_size')::int, age(second_time, first_time),
    request_successes, request_failures, last_failure_error);
end;
$$ language plpgsql;
