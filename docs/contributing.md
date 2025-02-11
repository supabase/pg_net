pg_net is OSS. PR and issues are welcome.


## Development

[Nix](https://nixos.org/download.html) is required to set up the environment.

### Testing

For testing locally, execute:

```bash
# might take a while in downloading all the dependencies
$ nix-shell

# test on pg 12
$ nxpg-12 nxpg-build && net-with-nginx nxpg-12 nxpg-tmp nxpg-test

# test on pg 13
$ nxpg-13 nxpg-build && net-with-nginx nxpg-13 nxpg-tmp nxpg-test
```

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

These are scripts that wrap NixOps to deploy an AWS cloud setup. You must have `default` setup in `.aws/credentials`.

```bash
net-cloud-deploy
```

Then you can connect on the client instance and do requests to the server instance through `pg_net`.

```bash
net-cloud-ssh

psql -U postgres

select net.http_get('http://server');
# this the default welcome page of nginx on the server instance
# "server" is already included to /etc/hosts, so `curl http://server` will give the same result

# do some load testing
select net.http_get('http://server') from generate_series(1,1000);
# run `top` on another shell(another `nixops ssh -d pg_net client`) to check the worker behavior
```

To destroy the cloud setup:

```bash
net-cloud-destroy
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
