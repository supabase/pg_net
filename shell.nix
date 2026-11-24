{
  xpgPkgs,
  pkgs,
  curl,
}:
let
  nginxCustom = pkgs.callPackage ./nix/nginxCustom.nix { };
  loadtest = pkgs.callPackage ./nix/loadtest.nix { };
  pythonDeps = with pkgs.python3Packages; [
    pytest
    psycopg
    sqlalchemy
  ];
  style = pkgs.writeShellScriptBin "net-style" ''
    ${pkgs.clang-tools}/bin/clang-format -i src/*
    ${pkgs.ruff}/bin/ruff format
    ${pkgs.findutils}/bin/find . -name '*.nix' -exec ${pkgs.nixfmt}/bin/nixfmt {} \+
  '';
  styleCheck = pkgs.writeShellScriptBin "net-style-check" ''
    ${pkgs.clang-tools}/bin/clang-format -i src/*
    ${pkgs.git}/bin/git diff-index --exit-code HEAD -- '*.c'
    ${pkgs.ruff}/bin/ruff check
    ${pkgs.findutils}/bin/find . -name '*.nix' -exec ${pkgs.nixfmt}/bin/nixfmt --check {} \+
  '';
  plpgsqlcheck14 = pkgs.callPackage ./nix/plpgsql-check.nix {
    postgresql = xpgPkgs.postgresql_14;
  };
  plpgsqlcheck15 = pkgs.callPackage ./nix/plpgsql-check.nix {
    postgresql = xpgPkgs.postgresql_15;
  };
  plpgsqlcheck16 = pkgs.callPackage ./nix/plpgsql-check.nix {
    postgresql = xpgPkgs.postgresql_16;
  };
  plpgsqlcheck17 = pkgs.callPackage ./nix/plpgsql-check.nix {
    postgresql = xpgPkgs.postgresql_17;
  };
  plpgsqlcheck18 = pkgs.callPackage ./nix/plpgsql-check.nix {
    postgresql = xpgPkgs.postgresql_18;
  };
  plpgsqlcheck19 = pkgs.callPackage ./nix/plpgsql-check.nix {
    postgresql = xpgPkgs.postgresql_19;
  };
in
pkgs.mkShell {
  buildInputs = [
    (xpgPkgs.xpg.withExtensions {
      extensions = {
        "14" = [ plpgsqlcheck14 ];
        "15" = [ plpgsqlcheck15 ];
        "16" = [ plpgsqlcheck16 ];
        "17" = [ plpgsqlcheck17 ];
        "18" = [ plpgsqlcheck18 ];
        "19" = [ plpgsqlcheck19 ];
      };
    })
    pythonDeps
    nginxCustom.nginxScript
    curl
    loadtest
    style
    styleCheck
    pkgs.ruff
    pkgs.python313Packages.pglast
  ];
  shellHook = ''
    export HISTFILE=.history
  '';
}
