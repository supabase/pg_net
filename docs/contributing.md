pg_curl_worker is OSS. PR and issues are welcome.


## Development

[Nix](https://nixos.org/download.html) is required to set up the environment.


### Testing
For testing locally, execute:

```bash
# might take a while in downloading all the dependencies
$ nix-shell

# test on pg 12
$ curl-with-pg-12 make installcheck

# test on pg 13
$ curl-with-pg-13 make installcheck
```

### Documentation

All public API must be documented. Building documentation requires python 3.6+


#### Install Dependencies

Install mkdocs, themes, and extensions.

```shell
pip install -r docs/requirements_docs.txt
```

#### Serving

To serve the documentation locally run

```shell
mkdocs serve
```

and visit the docs at [http://127.0.0.1:8000/pg_curl_worker/](http://127.0.0.1:8000/pg_curl_worker/)


