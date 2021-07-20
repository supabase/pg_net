Creating the All examples assume `pg_curl_worker` are created in a schema named `net`.

## HTTP

#### net.async_get

Create an async HTTP GET request returning a bigint id that can be used to retrieve the result.

```sql
--8<-- "test/expected/test_async_get.out"
```


#### net.cancel_request

Given an id produced from calling an `async_get`/`async_post`/etc, cancel the request. If the request was already completed, its result will no longer be retrievable.

```sql
--8<-- "test/expected/test_cancel_request.out"
```

#### net.is_complete

Given an id produced from calling an `async_get`/`async_post`/etc, check if the request has been completed.

```sql
--8<-- "test/expected/test_is_complete.out"
```

#### net.collect_respones

Given an id produced from calling an `async_get`/`async_post`/etc, retrieve the response, blocking until the response is available.

It is recommended that [statement_timeout](https://www.postgresql.org/docs/13/runtime-config-client.html) is set for the maximum amount of time the user is willing to wait in case the background worker is overloaded.

```sql
--8<-- "test/expected/test_collect_response.out"
```
