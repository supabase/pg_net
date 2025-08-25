pg_net is OSS. PR and issues are welcome.


## Development

[Nix](https://nixos.org/download.html) is required to set up the environment and [Cachix](https://docs.cachix.org/installation) for cache usage.


### Testing

For testing locally, execute:

```bash
$ cachix use nxpg

# might take a while in downloading all the dependencies
$ nix-shell

# test on latest pg
$ xpg test

# test on pg 12
$ xpg -v 12 test

# test on pg 13
$ xpg -v 13 test
```

This will spawn a local db and an nginx server for testing.

### Debugging

You can turn on logging level to see curl traces with

```
$ export LOG_MIN_MESSAGES=debug2
```

```sql
select net.http_get('http://localhost:3000/projects');

-- * Trying ::1:3000...
-- * connect to ::1 port 3000 failed: Connection refused
-- * Trying 127.0.0.1:3000...
-- * Connected to localhost (127.0.0.1) port 3000 (#0)
-- > GET /projects HTTP/1.1
-- Host: localhost:3000
-- Accept: */*
-- User-Agent: pg_net/0.2
--
-- * Mark bundle as not supporting multiuse
-- < HTTP/1.1 200 OK
-- < Transfer-Encoding: chunked
-- < Date: Fri, 27 Aug 2021 00:14:37 GMT
-- < Server: postgrest/7.0.0 (UNKNOWN)
-- < Content-Type: application/json; charset=utf-8
-- < Content-Range: 0-58/*
-- < Content-Location: /projects
-- <
-- * Connection #0 to host localhost left intact
```

### GDB

To debug the background worker, there's a script that wraps GDB. It automatically obtains the pid of the latest started worker:

```
$ nix-shell
$ sudo net-with-gdb
```

## Load Testing

The `net-loadtest` launchs a temporary db and a nginx server, waiting until a number of requests are done, then reporting results (plus process monitoring) at the end.

It takes a two parameters: the number of requests (GETs) and the `pg_net.batch_size`.

```bash
$ net-loadtest 1000 200
...

## Loadtest results

|   requests |   batch_size | time_taken      |   request_successes |   request_failures | last_failure_error   |
|-----------:|-------------:|:----------------|--------------------:|-------------------:|:---------------------|
|       1000 |          200 | 00:00:04.198886 |                1000 |                  0 |                      |

## Loadtest elapsed seconds vs CPU/MEM

|   Elapsed time |   CPU (%) |   Real (MB) |   Virtual (MB) |
|----------------|-----------|-------------|----------------|
|          0     |         0 |      20.227 |        437.426 |
|          1     |         2 |      20.227 |        437.566 |
|          2.001 |         4 |      20.418 |        437.566 |
|          3.002 |         3 |      20.418 |        437.566 |
```

## Documentation

All public API must be documented. Building documentation requires python 3.6+


### Install Dependencies

Install mkdocs, themes, and extensions.

```shell
pip install -r docs/requirements_docs.txt
```

### Serving

To serve the documentation locally run

```shell
mkdocs serve
```

and visit the docs at [http://127.0.0.1:8000/pg_net/](http://127.0.0.1:8000/pg_net/)
