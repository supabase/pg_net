let
  flakeLock = builtins.fromJSON (builtins.readFile ./flake.lock);
  nixpkgsLock = flakeLock.nodes.nixpkgs.locked;
  xpgLock = flakeLock.nodes.xpg.locked;
in
{
  pkgs ? import (builtins.fetchTarball {
    name = nixpkgsLock.rev;
    url = "https://github.com/${nixpkgsLock.owner}/${nixpkgsLock.repo}/archive/${nixpkgsLock.rev}.tar.gz";
    sha256 = nixpkgsLock.narHash;
  }) { },
  xpgPkgs ? import (
    pkgs.fetchFromGitHub {
      inherit (xpgLock) owner repo rev;
      sha256 = xpgLock.narHash;
    }
  ),
  pgVersion ? null,
  cassert ? true,
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
    pkgs.curlWithGnuTls
    loadtest
    style
    styleCheck
    pkgs.ruff
  ];
  shellHook = ''
    export HISTFILE=.history
  '';
}
