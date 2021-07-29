Tested with PostgreSQL 12 and 13.

## Setup

### Server
Clone this repo and run

```bash
make && make install
```

To make the extension available to the database add on `postgresql.conf`:

```
shared_preload_libraries = 'pg_net'
```


### Database
To enable the extension in PostgreSQL we must execute a `create extension` statement. The extension creates its own schema/namespace named `net` to avoid naming conflicts.

```psql
create extension pg_net;
```
