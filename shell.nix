with import (builtins.fetchTarball {
    name = "24.05"; # May 31 2024
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/24.05.tar.gz";
    sha256 = "sha256:1lr1h35prqkd1mkmzriwlpvxcb34kmhc9dnr48gkm8hh089hifmx";
}) {};
mkShell {
  buildInputs =
    let
      nginxCustom = callPackage ./nix/nginxCustom.nix {};
      nixopsScripts = callPackage ./nix/nixopsScripts.nix {};
      xpg = callPackage ./nix/xpg.nix {inherit fetchFromGitHub;};
      loadtest = callPackage ./nix/loadtest.nix {};
      pythonDeps = with python3Packages; [
        pytest
        psycopg2
        sqlalchemy
      ];
    in
    [
      xpg.xpg
      pythonDeps
      nginxCustom.nginxScript
      curl
      loadtest
    ] ++
    nixopsScripts;
  shellHook = ''
    export HISTFILE=.history
  '';
}
