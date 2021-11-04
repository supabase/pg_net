{ postgresql, openresty, writeShellScriptBin } :

let
  LOGMIN = builtins.getEnv "LOGMIN";
  logMin = if builtins.stringLength LOGMIN == 0 then "WARNING" else LOGMIN; # warning is the default in pg
  ver = builtins.head (builtins.splitVersion postgresql.version);
  script = ''
    export PATH=${postgresql}/bin:${openresty}/bin:"$PATH"

    tmpdir="$(mktemp -d)"

    export PGDATA="$tmpdir"
    export PGHOST="$tmpdir"
    export PGUSER=postgres
    export PGDATABASE=postgres

    trap 'pg_ctl stop -m i && rm -rf "$tmpdir" && openresty -s stop' sigint sigterm exit

    PGTZ=UTC initdb --no-locale --encoding=UTF8 --nosync -U "$PGUSER"

    options="-F -c listen_addresses=\"\" -c log_min_messages=${logMin} -k $PGDATA"

    ext_options="-c shared_preload_libraries=\"pg_net\""

    pg_ctl start -o "$options" -o "$ext_options"

    createdb contrib_regression

    openresty -p nix/nginx &

    psql -v ON_ERROR_STOP=1 -f test/fixtures.sql -d contrib_regression

    "$@"
  '';
in
writeShellScriptBin "net-with-pg-${ver}" script
