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
in with import nixpkgs {};
mkShell {
  buildInputs =
    let
      pgWithExt = { pg }: pg.withPackages (p: [ (callPackage ./nix/pg_net.nix { postgresql = pg;}) ]);
      pg12WithExt = pgWithExt { pg = postgresql_12;};
      pg13WithExt = pgWithExt { pg = postgresql_13;};
      net-with-pg-12 = callPackage ./nix/pgScript.nix { postgresql = pg12WithExt; openresty = oldOpenresty; };
      net-with-pg-13 = callPackage ./nix/pgScript.nix { postgresql = pg13WithExt; openresty = oldOpenresty; };
      valgrind-net-with-pg-12 = callPackage ./nix/pgValgrindScript.nix { postgresql = pg12WithExt; };
      pythonDeps = with pythonPackages; [
        pytest
        psycopg2
        sqlalchemy
      ];
      format = callPackage ./nix/format.nix {};
    in
    [
      net-with-pg-12
      net-with-pg-13
      valgrind-net-with-pg-12
      pythonDeps
      nixops
      format.do format.doCheck
    ];
  shellHook = ''
    export NIX_PATH="nixpkgs=${nixpkgs}:."
    export NIXOPS_STATE=".deployment.nixops"
  '';
}
