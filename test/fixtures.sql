create extension pg_net;
alter system set pg_net.ttl TO '4 seconds';
select pg_reload_conf();
