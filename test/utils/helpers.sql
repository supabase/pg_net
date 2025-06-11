create or replace function is_worker_up() returns bool as $$
  select exists(select pid from pg_stat_activity where backend_type ilike '%pg_net%');
$$ language sql;

create or replace function kill_worker() returns bool as $$
  select pg_terminate_backend(pid) from pg_stat_activity where backend_type ilike '%pg_net%';
$$ language sql;
