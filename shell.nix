with import (builtins.fetchTarball {
    name = "24.05"; # May 31 2024
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/24.05.tar.gz";
    sha256 = "sha256:1lr1h35prqkd1mkmzriwlpvxcb34kmhc9dnr48gkm8hh089hifmx";
}) {};
mkShell {
  buildInputs =
    let
      pidFileName = "net_worker.pid";
      gdbScript = callPackage ./nix/gdbScript.nix {inherit pidFileName;};
      nginxCustom = callPackage ./nix/nginxCustom.nix {};
      nixopsScripts = callPackage ./nix/nixopsScripts.nix {};
      pythonDeps = with python3Packages; [
        pytest
        psycopg2
        sqlalchemy
      ];
      format = callPackage ./nix/format.nix {};
      nxpg = callPackage ./nix/nxpg.nix {inherit pidFileName;};
    in
    [
      pythonDeps
      format.do format.doCheck
      nginxCustom.nginxScript
      curl
    ] ++
    nixopsScripts ++
    lib.optional stdenv.isLinux [gdbScript] ++
    nxpg;
  shellHook = ''
    export HISTFILE=.history
  '';
}
