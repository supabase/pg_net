create extension pg_net;
alter system set pg_net.ttl TO '2 seconds';
select pg_reload_conf();
