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

### Reproduble setup for load testing

This will deploy a client and server on t3a.nano. You must have `supabase-dev` setup in `.aws/credentials`.

```bash
nixops create nix/deploy.nix -d pg_net

nixops deploy -d pg_net
# will take a while
```

Then you can connect on the client instance and do requests to the server instance through `pg_net`.

```bash
nixops ssh -d pg_net client

psql -U postgres

create extension pg_net;

select net.http_get('http://server');
# this the default welcome page of nginx on the server instance
# "server" is already included to /etc/hosts, so `curl http://server` will give the same result

# do some load testing
select net.http_get('http://server') from generate_series(1,1000);
# run `top` on another shell(another `nixops ssh -d pg_net client`) to check the worker behavior
```

#### Testing a slow response

```bash
nixops ssh -d pg_net client

# takes 32 seconds to respond
select net.http_get('http://server/slow-reply');
```

#### Destroying the setup

To destroy the instances:

```bash
nixops destroy -d pg_net --confirm
nixops delete -d pg_net
```

### Run Valgrind to check memory leaks

Start a postgres instance under valgrind.

```
valgrind-net-with-pg-12

# it should show this output:
# Connect to this db from another shell using the following command:
#
#        psql -h <path> -U postgres

# connect to the db in another shell
psql -h <path> -U postgres

create extension pg_net;
select net.http_get('https://supabase.io');

# stop the db instance
# then see the valgrind results in "valgrindlog"
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
