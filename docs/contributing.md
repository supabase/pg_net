pg_net is OSS. PR and issues are welcome.


## Development

[Nix](https://nixos.org/download.html) is required to set up the environment.

### Testing

For testing locally, execute:

```bash
# might take a while in downloading all the dependencies
$ nix-shell

# test on pg 12
$ net-with-pg-12 python -m pytest -vv"

# test on pg 13
$ net-with-pg-13 python -m pytest -vv"
```

### Debugging

You can turn on logging level to see curl traces with

```
$ export LOGMIN=DEBUG1 # must be done outside nix-shell, then go in nix-shell as usual

$ nix-shell

$ net-with-pg-12 psql
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

To debug the background worker, grab its PID from the logs:

```
$ nix-shell
$ net-with-pg-16 psql

2024-09-02 20:16:26.905 -05 [1145879] INFO:  pg_net_worker started with a config of: pg_net.ttl=6 hours, pg_net.batch_size=200, pg_net.database_name=postgres
```

And use it like:

```
$ nix-shell
$ sudo with-gdb -p 1145879
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
