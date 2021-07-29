## HTTP

### net.http_get

##### description
Create an HTTP GET request returning the request's id

!!! note
    HTTP requests are not started until the transaction is committed


##### signature
```sql
net.http_get(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb DEFAULT '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb DEFAULT '{}'::jsonb,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int DEFAULT 1000,
    -- the minimum amount of time the response should be persisted
    ttl interval default '3 days',
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
Create an HTTP POST request returning the request's id

!!! note
    HTTP requests are not started until the transaction is committed


##### signature
```sql
net.http_post(
    -- url for the request
    url text,
    -- body of the POST request
    body bytea default null,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb DEFAULT '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb DEFAULT '{"Content-Type": "application/x-www-form-urlencoded"}'::jsonb,
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int DEFAULT 1000,
    -- the minimum amount of time the response should be persisted
    ttl interval default '3 days'
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
        body:='{"hello": "world"}',
        headers:='{"content-type": "application/json", "accept": "application/json"}'
    );
request_id
----------
         1
(1 row)
```

### net.http_collect_response

##### description 
Given a `request_id` reference, retrieve the response.

When `async:=false` is set it is recommended that [statement_timeout](https://www.postgresql.org/docs/13/runtime-config-client.html) is set for the maximum amount of time the caller is willing to wait in case the response is slow to populate.

!!! warning
    `net.http_collect_response` must be in a separate transaction from the calls to `net.http_<method>`


##### signature
```sql
net.http_collect_response(
    -- request_id reference
    request_id id,
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- http response composite wrapped in a result type
    returns net.http_response_result

    strict
    volatile
    parallel safe
```

##### usage
```sql
select net.http_get('https://news.ycombinator.com') as request_id;
request_id
----------
         1
(1 row)

select * from net.http_collect_response(1, async:=false);
status  | message | response
--------+---------+----------
SUCCESS        ok        ...
```
where `response` is a composite
```sql
status_code integer
headers jsonb
body bytea
```

Possible values for `net.http_response_result.status` are `('PENDING', 'SUCCESS', 'ERROR')`
