create schema if not exists http;

-- Store pending requests. The background worker reads from here
-- API: Private
create table http.request_queue (
	id bigserial primary key,
	url text,
	params jsonb not null,
	headers jsonb not null,
	timeout_seconds numeric not null,
	curl_opts jsonb not null
);

-- Associates a response with a request
-- API: Private
create table http.response (
	id bigint primary key references http.request_queue(id) on delete cascade,
	http_status_code integer,
	content_type text,
	headers jsonb,
	content text
);

-- Interface to make an async request
-- API: Public
create or replace function http.async_get(
	url text,
	params jsonb DEFAULT '{}'::jsonb,
	headers jsonb DEFAULT '{}'::jsonb,
	timeout_seconds numeric DEFAULT 1,
	curl_opts jsonb DEFAULT '{}'::jsonb
)
    returns bigint
    language sql
    volatile
	parallel safe
	strict
as $$
	insert into http.request_queue(url, params, headers, timeout_seconds, curl_opts)
	values (url, params, headers, timeout_seconds, curl_opts)
	returning id;
$$;

-- Check if a request is complete
-- API: Public
create or replace function http.is_complete(
    request_id bigint
)
    returns bool
    language sql
    stable
	parallel safe
	strict
as $$
    select (count(1) > 0)::bool
    from http.response
    where id = request_id;
$$;

-- Cancel a request in the queue
-- API: Public
create or replace function http.cancel_request(
    request_id bigint
)
    returns bigint
    language sql
    volatile
	parallel safe
	strict
as $$
    delete
    from http.request_queue
    where id = request_id
    returning id;
$$;
