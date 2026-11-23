-- pg_net used to grant PUBLIC everything on net: usage on the schema, all on the tables and the
-- sequence, and every function was callable. So every role could send requests, read everyone's
-- responses, and plant a trigger on the queue that runs as the worker. No role could be locked
-- out, and "revoke from PUBLIC" broke the worker.
--
-- From here on pg_net grants nothing on the schema, the tables and the sequence. A role gets them
-- from whoever administers the database, like pg_cron does. See the README.
--
-- Privileges add up: a role can do whatever it holds itself plus whatever PUBLIC holds. So as long
-- as PUBLIC keeps anything here, revoking it from one role does nothing.
revoke usage on schema net from public;
revoke all on all tables in schema net from public;
revoke all on all sequences in schema net from public;
-- Only the owner may restart the worker. Every other function runs as the caller and touches the
-- tables, so the table grants decide what a role can do with it.
revoke execute on function net.worker_restart() from public;
