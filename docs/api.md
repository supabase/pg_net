## HTTP

### net.http_get

##### description
Create an HTTP GET request returning the request's id

!!! note
    HTTP requests are not started until the transaction is committed

!!! note
    this is a Postgres SECURITY DEFINER function

##### signature
```sql
net.http_get(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{}'::jsonb,
    -- WARNING: this is currently ignored, so there is no timeout
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 1000
)
    -- request_id reference
    returns bigint

    strict
    volatile
    parallel safe
    language plpgsql
```

##### usage
```sql
select net.http_get('https://news.ycombinator.com') as request_id;
request_id
----------
         1
(1 row)
```

### net.http_post

##### description
Create an HTTP POST request with a JSON body, returning the request's id

!!! note
    HTTP requests are not started until the transaction is committed

!!! note
    the body's character set encoding matches the database's `server_encoding` setting

!!! note
    this is a Postgres SECURITY DEFINER function

##### signature
```sql
net.http_post(
    -- url for the request
    url text,
    -- body of the POST request
    body jsonb default '{}'::jsonb,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{"Content-Type": "application/json"}'::jsonb,
    -- WARNING: this is currently ignored, so there is no timeout
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 1000
)
    -- request_id reference
    returns bigint

    volatile
    parallel safe
    language plpgsql
```

##### usage
```sql
select
    net.http_post(
        url:='https://httpbin.org/post',
        body:='{"hello": "world"}'::jsonb
    ) as request_id;
request_id
----------
         1
(1 row)
```
