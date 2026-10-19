create table run (
  requests int,
  batch_size int,
  time_taken interval,
  request_successes bigint,
  request_failures bigint,
  last_failure_error text
);

-- random delay in ms from a normal distribution with the given mean and standard deviation.
--
-- Uses the Box-Muller transform to turn two independent uniform samples u1, u2 into a standard normal
-- value z = sqrt(-2 * ln(u1)) * cos(2 * pi * u2), which is then scaled to mean_ms + sd_ms * z.
-- PostgreSQL random() returns values in [0, 1) and ln(0) is undefined, so u1 is taken as `1 - random()`,
-- which lies in (0, 1]. Negative results are clamped to 0 since a delay can't be negative.
create or replace function random_delay_ms(mean_ms int, sd_ms int) returns int as $$
  select greatest(0, round(mean_ms + sd_ms * sqrt(-2 * ln(1 - random())) * cos(2 * pi() * random())))::int
$$ language sql volatile;

-- loadtest using many gets, used to be called `repro_timeouts`
--
-- when delay_ms is given, each request goes to the nginx /delay endpoint with a server delay drawn
-- from a normal distribution with mean delay_ms and standard deviation delay_sd_ms (clamped
-- at 0), to simulate the latency of real servers
create or replace procedure wait_for_many_gets(
  number_of_requests int default 10000,
  url text default 'http://localhost:8080',
  delay_ms int default null,
  delay_sd_ms int default 0
) as $$
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
      net.http_get(
        case when delay_ms is null then url
        else url || '/delay?s=' || round(random_delay_ms(delay_ms, delay_sd_ms) / 1000.0, 3)
        end
      ) as id
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
