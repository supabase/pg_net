{ stdenv, lib, makeWrapper, fetchurl, writeShellScriptBin, findutils, entr, callPackage, lcov, pidFileName, gnused, python3 } :
let
  prefix = "nxpg";
  ourPg = callPackage ./postgresql {
    inherit lib;
    inherit stdenv;
    inherit fetchurl;
    inherit makeWrapper;
    inherit callPackage;
  };
  supportedPgs = [
    ourPg.postgresql_17
    ourPg.postgresql_16
    ourPg.postgresql_15
    ourPg.postgresql_14
    ourPg.postgresql_13
    ourPg.postgresql_12
  ];
  build =
    writeShellScriptBin "${prefix}-build" ''
      set -euo pipefail

      make clean
      make
    '';
  buildCov =
    writeShellScriptBin "${prefix}-build-cov" ''
      set -euo pipefail

      make clean
      make COVERAGE=1
    '';
  test =
    writeShellScriptBin "${prefix}-test" ''
      set -euo pipefail

      ${python3}/bin/python -m pytest -vv "$@"
    '';
  cov =
    writeShellScriptBin "${prefix}-coverage" ''
      set -euo pipefail

      info_file="coverage.info"
      out_dir="coverage_html"

      ${python3}/bin/python -m pytest -vv "$@"

      ${lcov}/bin/lcov --capture --directory . --output-file "$info_file"

      # remove postgres headers on the nix store, otherwise they show on the output
      ${lcov}/bin/lcov --remove "$info_file" '/nix/*' --output-file "$info_file" || true

      ${lcov}/bin/lcov --list coverage.info
      ${lcov}/bin/genhtml "$info_file" --output-directory "$out_dir"

      echo "${prefix}-coverage: To see the results, visit file://$(pwd)/$out_dir/index.html on your browser"
    '';
  watch =
    writeShellScriptBin "${prefix}-watch" ''
      set -euo pipefail

      ${findutils}/bin/find . -type f \( -name '*.c' -o -name '*.h' \) | ${entr}/bin/entr -dr "$@"
    '';

  tmpDb =
    writeShellScriptBin "${prefix}-tmp" ''
      set -euo pipefail

      export tmpdir="$(mktemp -d)"

      export PGDATA="$tmpdir"
      export PGHOST="$tmpdir"
      export PGUSER=postgres
      export PGDATABASE=postgres

      trap 'pg_ctl stop -m i && rm -rf "$tmpdir" && rm ${pidFileName}' sigint sigterm exit

      PGTZ=UTC initdb --no-locale --encoding=UTF8 --nosync -U "$PGUSER"

      # pg versions older than 16 don't support adding "-c" to initdb to add these options
      # so we just modify the resulting postgresql.conf to avoid an error
      echo "dynamic_library_path='\$libdir:$(pwd)'" >> $PGDATA/postgresql.conf
      echo "extension_control_path='\$system:$(pwd)'" >> $PGDATA/postgresql.conf

      options="-F -c listen_addresses=\"\" -c log_min_messages=\"''${LOG_MIN_MESSAGES:-INFO}\" -k $PGDATA"

      ext_options="-c shared_preload_libraries=\"pg_net\""

      pg_ctl start -o "$options" -o "$ext_options"

      psql -v ON_ERROR_STOP=1 -c "create database pre_existing" -d postgres

      psql -v ON_ERROR_STOP=1 -c "create role pre_existing nosuperuser login" -d postgres

      psql -v ON_ERROR_STOP=1 -c "create extension pg_net" -d postgres

      psql -t -c "\o ${pidFileName}" -c "select pid from pg_stat_activity where backend_type ilike '%pg_net%'"

      ${gnused}/bin/sed '/^''$/d;s/[[:blank:]]//g' -i ${pidFileName}

      psql -f ${./bench.sql}

      "$@"
    '';
    allPgPaths = map (pg:
      let
        ver = builtins.head (builtins.splitVersion pg.version);
        script = ''
          set -euo pipefail

          export PATH=${pg}/bin:"$PATH"

          "$@"
        '';
      in
      writeShellScriptBin "${prefix}-${ver}" script
    ) supportedPgs;

    netWith = map (pg:
        let
          ver = builtins.head (builtins.splitVersion pg.version);
          script = ''
            set -euo pipefail
            export PATH=${pg}/bin:"$PATH"
            ${buildCov}/bin/${prefix}-build-cov
            ${tmpDb}/bin/${prefix}-tmp "$@"
          '';
        in
        writeShellScriptBin "net-with-pg-${ver}" script
    ) supportedPgs;
in
[
  build
  buildCov
  test
  cov
  watch
  tmpDb
  allPgPaths
  netWith
]
