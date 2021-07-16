create schema if not exists http;

-- Store pending requests. The background worker reads from here
-- API: Private
create table http.request_queue (
	id bigserial primary key,
	url text not null,
	params jsonb not null,
	headers jsonb not null,
	timeout_seconds numeric,
	curl_opts jsonb not null,
	is_completed bool default false,
    -- Available for delete after this date
    delete_after timestamp not null
);
create index ix_request_queue_delete_after on http.request_queue (delete_after);


-- Associates a response with a request
-- API: Private
create table http.response (
	id bigint primary key references http.request_queue(id) on delete cascade,
	http_status_code integer,
	content_type text,
	headers jsonb,
	body text
);

-- Interface to make an async request
-- API: Public
create or replace function http.async_get(
	url text,
	params jsonb DEFAULT '{}'::jsonb,
	headers jsonb DEFAULT '{}'::jsonb,
	timeout_seconds numeric DEFAULT 1,
	curl_opts jsonb DEFAULT '{}'::jsonb,
    ttl interval default '3 days'
)
    returns bigint
    language plpgsql
    volatile
	parallel safe
	strict
as $$
declare
   request_id bigint; 
begin
    -- Add to the request queue
	insert into http.request_queue(url, params, headers, timeout_seconds, curl_opts, delete_after)
	values (url, params, headers, timeout_seconds, curl_opts, timezone('utc', now()) + ttl)
	returning id
    into request_id;

    -- Deleting a few requests exceeding their TTL
    with to_delete as (
        select id
        from http.request_queue
        where delete_after < (timezone('utc', now()))
        limit 3
    )
    delete from http.request_queue
    where id in (select id from to_delete);

    return request_id;
end;
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
