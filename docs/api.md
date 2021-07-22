## HTTP

### net.http_get

##### description
Create an HTTP GET request returning the request's uuid id. 

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
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- request_id reference
    returns uuid
    strict
    volatile
    parallel safe
    language plpgsql
```

##### usage
```sql
--8<-- "test/expected/test_http_get.out"
```

### net.http_collect_response

##### description 
Given a `request_id` reference, retrieve the response.

When `async:=false` is set it is recommended that [statement_timeout](https://www.postgresql.org/docs/13/runtime-config-client.html) is set for the maximum amount of time the caller is willing to wait in case the response is slow to populate.


##### signature
```sql
net.http_collect_response(
    -- request_id reference
    request_id uuid,
    -- when `true`, return immediately. when `false` wait for the request to complete before returning
    async bool default true
)
    -- http response composite type
    returns net.http_response
    strict
    volatile
    parallel safe
```

##### usage
```sql
--8<-- "test/expected/test_http_collect_response.out"
```


### net.http_cancel_request

##### description
Given a `request_id` reference, cancel the request. If the request was already completed, its result will no longer be retrievable.


##### signature
```sql
net.http_cancel_request(
    -- request_id reference
    request_id uuid
)
    -- request_id reference
    returns uuid
    strict
    volatile
    parallel safe
```

##### description
Given an id produced from calling an `async_get`/`async_post`/etc, cancel the request. If the request was already completed, its result will no longer be retrievable.

##### usage
```sql
--8<-- "test/expected/test_http_cancel_request.out"
```

## SMTP

Coming soon
