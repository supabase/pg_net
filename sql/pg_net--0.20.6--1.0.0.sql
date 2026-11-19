-- pg_net used to grant PUBLIC everything on net: usage on the schema, all on the tables and the
-- sequence, and every function was callable. So every role could send requests, read everyone's
-- responses, and plant a trigger on the queue that runs as the worker. No role could be locked
-- out, and "revoke from PUBLIC" broke the worker.
--
-- From here on pg_net grants nothing. A role gets usage on the schema and execute on the http
-- functions from whoever administers the database, like pg_cron does. See the README.
--
-- Privileges add up: a role can do whatever it holds itself plus whatever PUBLIC holds. So as long
-- as PUBLIC keeps anything here, revoking it from one role does nothing. Every object has to be
-- taken from PUBLIC for per-role grants to mean anything.
revoke usage on schema net from public;
revoke all on all tables in schema net from public;
revoke all on all sequences in schema net from public;
-- Postgres gives PUBLIC execute on every new function. The http functions below become SECURITY
-- DEFINER, so with PUBLIC execute any role with usage on the schema could queue requests as the
-- owner, and worker_restart() and wake() would let any role stop the worker for everyone.
revoke execute on all functions in schema net from public;

-- SECURITY DEFINER: the http functions run as the extension owner. As SECURITY INVOKER every
-- caller would also need insert on the queue table, usage on its sequence and execute on wake()
-- and the two helpers, and with those it could insert any row by hand. As definer the caller needs
-- execute on the function and nothing else, and the functions stay the only way to queue a
-- request, the same way cron.schedule() is the only way into pg_cron's job table.
--
-- The search_path is pinned because a definer body resolves unqualified names, like jsonb_each_text,
-- in the caller's search_path, and the caller could put its own there. Everything in net is written
-- with the schema prefix, so pg_catalog and pg_temp are all the body needs.
alter function net.http_get(text, jsonb, jsonb, integer)
    security definer set search_path = pg_catalog, pg_temp;
alter function net.http_post(text, jsonb, jsonb, jsonb, integer)
    security definer set search_path = pg_catalog, pg_temp;
alter function net.http_delete(text, jsonb, jsonb, integer, jsonb)
    security definer set search_path = pg_catalog, pg_temp;
