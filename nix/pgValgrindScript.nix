{ postgresql, valgrind, writeShellScriptBin } :

let
  LOGMIN = builtins.getEnv "LOGMIN";
  ver = builtins.head (builtins.splitVersion postgresql.version);
  valgrindLogFile = "valgrindlog";
  script = ''
    export PATH=${postgresql}/bin:"$PATH"

    tmpdir="$(mktemp -d)"

    export PGDATA="$tmpdir"
    export PGHOST="$tmpdir"
    export PGUSER=postgres
    export PGDATABASE=postgres

    PGTZ=UTC initdb --no-locale --encoding=UTF8 --nosync -U "$PGUSER"

    rm ${valgrindLogFile}

    echo -e "Connect to this db from another shell using the following command: \n"
    echo -e "\t psql -h "$tmpdir" -U postgres \n"

    ${valgrind}/bin/valgrind --log-file=${valgrindLogFile} --gen-suppressions=all \
    --leak-check=yes --trace-children=yes --track-origins=yes --suppressions=${./valgrind.supp} \
    postgres -c shared_preload_libraries="pg_net" -c listen_addresses="" -c autovacuum="off" -k $PGDATA

    rm -rf "$tmpdir"
  '';
in
writeShellScriptBin "net-with-valgrind-pg-${ver}" script
