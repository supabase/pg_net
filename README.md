## Curl background worker
<p>

<a href="https://github.com/supabase/pg_curl_worker/actions"><img src="https://github.com/supabase/pg_curl_worker/actions/workflows/main.yml/badge.svg" alt="Tests" height="18"></a>
<a href=""><img src="https://img.shields.io/badge/postgresql-12+-blue.svg" alt="PostgreSQL version" height="18"></a>

</p>



[Background worker](https://www.postgresql.org/docs/current/bgworker.html) that does http requests with curl without blocking.

### Usage

Do:

```shell
nix-shell --run "curl-with-pg-12 psql"
```

(Requires [Nix](https://nixos.org/download.html#nix-quick-install))

You should see these logs every 10 seconds

```
postgres@postgres=#

INFO:  GET of https://supabase.io/ returned http status code 200

INFO:  GET of https://aws.amazon.com/ returned http status code 200

INFO:  GET of https://www.wikipedia.org returned http status code 200

INFO:  GET of https://news.ycombinator.com/ returned http status code 200
```

### Next steps

- [ ] Create a requests table and populate it
- [ ] Read rows from the requests table inside the background worker(use [postgres SPI](https://www.postgresql.org/docs/current/spi.html))
- [ ] Create a [curl multi handle](https://curl.se/libcurl/c/curl_multi_add_handle.html) for each of the rows and make the requests
- [ ] Integrate this logic into a fork of [pgsql-http](https://github.com/pramsey/pgsql-http/)
