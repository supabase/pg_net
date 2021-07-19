create schema if not exists net;

-- Store pending requests. The background worker reads from here
-- API: Private
create table net.request_queue (
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
create index ix_request_queue_delete_after on net.request_queue (delete_after);


-- Associates a response with a request
-- API: Private
create table net.response (
	id bigint primary key references net.request_queue(id) on delete cascade,
	net_status_code integer,
	content_type text,
	headers jsonb,
	body text,
    timed_out bool
);

-- Interface to make an async request
-- API: Public
create or replace function net.async_get(
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
	insert into net.request_queue(url, params, headers, timeout_seconds, curl_opts, delete_after)
	values (url, params, headers, timeout_seconds, curl_opts, timezone('utc', now()) + ttl)
	returning id
    into request_id;

    -- Deleting a few requests exceeding their TTL
    with to_delete as (
        select id
        from net.request_queue
        where delete_after < (timezone('utc', now()))
        limit 3
    )
    delete from net.request_queue
    where id in (select id from to_delete);

    return request_id;
end;
$$;

-- Check if a request is complete
-- API: Public
create or replace function net.is_complete(
    request_id bigint
)
    returns bool
    language sql
    stable
	parallel safe
	strict
as $$
    select (count(1) > 0)::bool
    from net.response
    where id = request_id;
$$;

-- Cancel a request in the queue
-- API: Public
create or replace function net.cancel_request(
    request_id bigint
)
    returns bigint
    language sql
    volatile
	parallel safe
	strict
as $$
    delete
    from net.request_queue
    where id = request_id
    returning id;
$$;
