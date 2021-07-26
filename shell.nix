with import (builtins.fetchTarball {
  name = "2020-12-22";
  url = "https://github.com/NixOS/nixpkgs/archive/2a058487cb7a50e7650f1657ee0151a19c59ec3b.tar.gz";
  sha256 = "1h8c0mk6jlxdmjqch6ckj30pax3hqh6kwjlvp2021x3z4pdzrn9p";
}) {};
let
  net_worker = { postgresql }:
    stdenv.mkDerivation {
      name = "net_worker";
      buildInputs = [ postgresql curl ];
      src = ./.;
      installPhase = ''
        mkdir -p $out/bin
        install -D pg_net.so -t $out/lib

        install -D -t $out/share/postgresql/extension sql/*.sql
        install -D -t $out/share/postgresql/extension pg_net.control
      '';
    };
  pgWithExt = { postgresql } :
    let pg = postgresql.withPackages (p: [ (net_worker {inherit postgresql;}) ]);
        # Do `export LOGMIN=DEBUG2` outside nix-shell to get more detailed logging
        LOGMIN = builtins.getEnv "LOGMIN";
        logMin = if LOGMIN == "" then "WARNING" else LOGMIN;
    in ''
      export PATH=${pg}/bin:"$PATH"

      tmpdir="$(mktemp -d)"

      export PGDATA="$tmpdir"
      export PGHOST="$tmpdir"
      export PGUSER=postgres
      export PGDATABASE=postgres

      trap 'pg_ctl stop -m i && rm -rf "$tmpdir"' sigint sigterm exit

      PGTZ=UTC initdb --no-locale --encoding=UTF8 --nosync -U "$PGUSER"

      options="-F -c listen_addresses=\"\" -c log_min_messages=${logMin} -k $PGDATA"

      ext_options="-c shared_preload_libraries=\"pg_net\""

      pg_ctl start -o "$options" -o "$ext_options"

      createdb contrib_regression

      psql -v ON_ERROR_STOP=1 -f test/fixtures.sql -d contrib_regression

      "$@"
    '';
  net-with-pg-12 = writeShellScriptBin "net-with-pg-12" (pgWithExt { postgresql = postgresql_12; });
  net-with-pg-13 = writeShellScriptBin "net-with-pg-13" (pgWithExt { postgresql = postgresql_13; });

  my-python-packages = python-packages: with python-packages; [
    pytest
    psycopg2-binary
    sqlalchemy
  ]; 
  python-with-my-packages = python3.withPackages my-python-packages;

in
mkShell {
  buildInputs = [ net-with-pg-12 net-with-pg-13 ];
}
