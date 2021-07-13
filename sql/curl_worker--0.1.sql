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
	id bigint primary key references http.request_queue(id),
	http_status_code integer,
	content_type text,
	headers jsonb,
	content text
);

-- Functional interface to make an async request
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
