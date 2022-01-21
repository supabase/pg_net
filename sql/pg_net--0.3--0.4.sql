alter function net.http_get(url text, params jsonb, headers jsonb, timeout_milliseconds integer) security definer;
alter function net.http_post(url text, body jsonb, params jsonb, headers jsonb, timeout_milliseconds integer) security definer;
alter function net.http_collect_response(request_id bigint, async boolean) security definer;
