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
    request_id bigint,
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
select
    net.http_post(
        url:='https://httpbin.org/post',
        body:='{"hello": "world"}'::jsonb
    ) as request_id;
request_id
----------
         1
(1 row)

select * from net.http_collect_response(1, async:=false);
status  | message | response
--------+---------+----------
SUCCESS        ok   (
                      status_code := 200,
                      headers     := '{"date": ...}',
                      body        := '{"args": ...}'
                    )::net.http_response_result


select
    (response).body::json
from
    net.http_collect_response(request_id:=1);
                               body
-------------------------------------------------------------------
 {
   "args": {},
   "data": "{\"hello\": \"world\"}",
   "files": {},
   "form": {},
   "headers": {
     "Accept": "*/*",
     "Content-Length": "18",
     "Content-Type": "application/json",
     "Host": "httpbin.org",
     "User-Agent": "pg_net/0.1",
     "X-Amzn-Trace-Id": "Root=1-61031a5c-7e1afeae69bffa8614d8e48e"
   },
   "json": {
     "hello": "world"
   },
   "origin": "135.63.38.488",
   "url": "https://httpbin.org/post"
 }
(1 row)
```

where `response` is a composite

```sql
status_code integer
headers jsonb
body text
```
Possible values for `net.http_response_result.status` are `('PENDING', 'SUCCESS', 'ERROR')`
