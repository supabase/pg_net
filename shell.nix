let
  oldNixpkgs = (import (builtins.fetchTarball {
    name = "2020-04-20";
    url = "https://github.com/NixOS/nixpkgs/archive/5272327b81ed355bbed5659b8d303cf2979b6953.tar.gz";
    sha256 = "0182ys095dfx02vl2a20j1hz92dx3mfgz2a6fhn31bqlp1wa8hlq";
  }){});
  nixpkgs = builtins.fetchTarball {
    name = "24.05"; # May 31 2024
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/24.05.tar.gz";
    sha256 = "sha256:1lr1h35prqkd1mkmzriwlpvxcb34kmhc9dnr48gkm8hh089hifmx";
  };
in with import nixpkgs {};
mkShell {
  buildInputs =
    let
      supportedPgVersions = [
        postgresql_12
        postgresql_13
        postgresql_14
        postgresql_15
        postgresql_16
      ];
      pgWithExt = { pg }: pg.withPackages (p: [ (callPackage ./nix/pg_net.nix { postgresql = pg;}) ]);
      extAll = map (x: callPackage ./nix/pgScript.nix { postgresql = pgWithExt { pg = x;}; }) supportedPgVersions;
      nginxScript = callPackage ./nix/nginxScript.nix {};
      pathodScript = callPackage ./nix/pathodScript.nix { mitmproxy = oldNixpkgs.mitmproxy; };
      gdbScript = callPackage ./nix/gdbScript.nix {};
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
      nginxScript
      pathodScript
      gdbScript
    ];
  shellHook = ''
    export HISTFILE=.history
  '';
}
