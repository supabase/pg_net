let
  # newer nginx/openresty versions somehow do not support using a prefix directory(-p)
  # and want to read from /var/nginx/
  oldOpenresty = (import (builtins.fetchTarball {
    name = "2020-04-20";
    url = "https://github.com/NixOS/nixpkgs/archive/5272327b81ed355bbed5659b8d303cf2979b6953.tar.gz";
    sha256 = "0182ys095dfx02vl2a20j1hz92dx3mfgz2a6fhn31bqlp1wa8hlq";
  }){}).openresty;
  nixpkgs = builtins.fetchTarball {
    name = "2020-12-22";
    url = "https://github.com/NixOS/nixpkgs/archive/2a058487cb7a50e7650f1657ee0151a19c59ec3b.tar.gz";
    sha256 = "1h8c0mk6jlxdmjqch6ckj30pax3hqh6kwjlvp2021x3z4pdzrn9p";
  };
  newPkgs = (import (builtins.fetchTarball {
    name = "2023-09-16";
    url = "https://github.com/NixOS/nixpkgs/archive/ae5b96f3ab6aabb60809ab78c2c99f8dd51ee678.tar.gz";
    sha256 = "11fpdcj5xrmmngq0z8gsc3axambqzvyqkfk23jn3qkx9a5x56xxk";
  }){});
in with import nixpkgs {};
mkShell {
  buildInputs =
    let
      supportedPgVersions = [
        postgresql_12
        postgresql_13
        newPkgs.postgresql_14
        newPkgs.postgresql_15
        newPkgs.postgresql_16
      ];
      pgWithExt = { pg }: pg.withPackages (p: [ (callPackage ./nix/pg_net.nix { postgresql = pg;}) ]);
      extAll = map (x: callPackage ./nix/pgScript.nix { postgresql = pgWithExt { pg = x;}; }) supportedPgVersions;
      valgrind-net-with-pg-12 = callPackage ./nix/pgValgrindScript.nix { postgresql = pgWithExt { pg = postgresql_12;}; };
      nginxScript = callPackage ./nix/nginxScript.nix { openresty = oldOpenresty; };
      pathodScript = callPackage ./nix/pathodScript.nix {};
      pythonDeps = with pythonPackages; [
        pytest
        psycopg2
        sqlalchemy
      ];
      format = callPackage ./nix/format.nix {};
    in
    [
      valgrind-net-with-pg-12
      extAll
      pythonDeps
      nixops
      format.do format.doCheck
      nginxScript
      pathodScript
      oldOpenresty
    ];
  shellHook = ''
    export NIX_PATH="nixpkgs=${nixpkgs}:."
    export NIXOPS_STATE=".deployment.nixops"
    export HISTFILE=.history
  '';
}
