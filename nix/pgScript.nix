{ postgresql, writeShellScriptBin } :

let
  ver = builtins.head (builtins.splitVersion postgresql.version);
  script = ''
    set -euo pipefail

    export PATH=${postgresql}/bin:"$PATH"

    tmpdir="$(mktemp -d)"

    export PGDATA="$tmpdir"
    export PGHOST="$tmpdir"
    export PGUSER=postgres
    export PGDATABASE=postgres

    trap 'pg_ctl stop -m i && rm -rf "$tmpdir"' sigint sigterm exit

    PGTZ=UTC initdb --no-locale --encoding=UTF8 --nosync -U "$PGUSER"

    options="-F -c listen_addresses=\"\" -c log_min_messages=\"''${LOG_MIN_MESSAGES:-INFO}\" -k $PGDATA"

    ext_options="-c shared_preload_libraries=\"pg_net\""

    pg_ctl start -o "$options" -o "$ext_options"

    psql -v ON_ERROR_STOP=1 -c "create extension pg_net" -d postgres

    "$@"
  '';
in
writeShellScriptBin "net-with-pg-${ver}" script
