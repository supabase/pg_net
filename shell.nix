with import (builtins.fetchTarball {
    name = "24.05"; # May 31 2024
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/24.05.tar.gz";
    sha256 = "sha256:1lr1h35prqkd1mkmzriwlpvxcb34kmhc9dnr48gkm8hh089hifmx";
}) {};
mkShell {
  buildInputs =
    let
      ourPg = callPackage ./nix/postgresql {
        inherit lib;
        inherit stdenv;
        inherit fetchurl;
        inherit makeWrapper;
        inherit callPackage;
      };
      pidFileName = "net_worker.pid";
      supportedPgVersions = [
        postgresql_12
        postgresql_13
        postgresql_14
        postgresql_15
        postgresql_16
        ourPg.postgresql_17
      ];
      pgWithExt = { pg }: pg.withPackages (p: [ (callPackage ./nix/pg_net.nix { postgresql = pg;}) ]);
      extAll = map (x: callPackage ./nix/pgScript.nix { postgresql = pgWithExt { pg = x;}; inherit pidFileName;}) supportedPgVersions;
      gdbScript = callPackage ./nix/gdbScript.nix {inherit pidFileName;};
      nginxCustom = callPackage ./nix/nginxCustom.nix {};
      nixopsScripts = callPackage ./nix/nixopsScripts.nix {};
      pythonDeps = with python3Packages; [
        pytest
        psycopg2
        sqlalchemy
      ];
      format = callPackage ./nix/format.nix {};
    in
    [
      extAll
      pythonDeps
      format.do format.doCheck
      nginxCustom.nginxScript
    ] ++
    nixopsScripts ++
    lib.optional stdenv.isLinux [gdbScript];
  shellHook = ''
    export HISTFILE=.history
  '';
}
