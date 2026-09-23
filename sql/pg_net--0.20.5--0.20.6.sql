revoke update on all sequences in schema net to PUBLIC;

revoke insert on all tables in schema net to PUBLIC;
revoke update on all tables in schema net to PUBLIC;

revoke delete on all tables in schema net to PUBLIC;
grant delete on net.http_request_queue to PUBLIC;

revoke truncate on all tables in schema net to PUBLIC;
grant truncate on net.http_request_queue to PUBLIC;

revoke references on all tables in schema net to PUBLIC;
revoke trigger on all tables in schema net to PUBLIC;
revoke maintain on all tables in schema net to PUBLIC;
