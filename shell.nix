let
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
      net-with-pg-12 = callPackage ./nix/pgScript.nix { postgresql = pg12WithExt; };
      net-with-pg-13 = callPackage ./nix/pgScript.nix { postgresql = pg13WithExt; };
      valgrind-net-with-pg-12 = callPackage ./nix/pgValgrindScript.nix { postgresql = pg12WithExt; };
      pythonDeps = with pythonPackages; [
        pytest
        psycopg2
        sqlalchemy
      ];
    in
    [
      net-with-pg-12
      net-with-pg-13
      valgrind-net-with-pg-12
      pythonDeps
      nixops
    ];
  shellHook = ''
    export NIX_PATH="nixpkgs=${nixpkgs}:."
    export NIXOPS_STATE=".deployment.nixops"
  '';
}
