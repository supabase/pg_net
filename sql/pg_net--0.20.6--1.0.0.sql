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
-- Postgres gives PUBLIC execute on every new function. With it, worker_restart() would let any
-- role stop the worker for everyone.
revoke execute on all functions in schema net from public;
