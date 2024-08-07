let
  nixpkgs = builtins.fetchTarball {
    name = "2020-12-22";
    url = "https://github.com/NixOS/nixpkgs/archive/2a058487cb7a50e7650f1657ee0151a19c59ec3b.tar.gz";
    sha256 = "1h8c0mk6jlxdmjqch6ckj30pax3hqh6kwjlvp2021x3z4pdzrn9p";
  };
  newPkgs = (import (builtins.fetchTarball {
    name = "24.05";
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/24.05.tar.gz";
    sha256 = "sha256:1lr1h35prqkd1mkmzriwlpvxcb34kmhc9dnr48gkm8hh089hifmx";
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
      nginxScript = callPackage ./nix/nginxScript.nix { nginx = newPkgs.nginx; nginxModules = newPkgs.nginxModules; };
      pathodScript = callPackage ./nix/pathodScript.nix {};
      pythonDeps = with pythonPackages; [
        pytest
        psycopg2
        sqlalchemy
      ];
      format = callPackage ./nix/format.nix {};
    in
    [
      extAll
      pythonDeps
      nixops
      format.do format.doCheck
      nginxScript
      pathodScript
    ];
  shellHook = ''
    export NIX_PATH="nixpkgs=${nixpkgs}:."
    export NIXOPS_STATE=".deployment.nixops"
    export HISTFILE=.history
  '';
}
