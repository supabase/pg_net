with import (builtins.fetchTarball {
    name = "25.05";
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/25.05.tar.gz";
    sha256 = "sha256:1915r28xc4znrh2vf4rrjnxldw2imysz819gzhk9qlrkqanmfsxd";
}) {};
mkShell {
  buildInputs =
    let
      nixpkgs2405 = import (builtins.fetchTarball {
          name = "24.05";
          url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/24.05.tar.gz";
          sha256 = "sha256:1lr1h35prqkd1mkmzriwlpvxcb34kmhc9dnr48gkm8hh089hifmx";
      }) {};
      nginxCustom = callPackage ./nix/nginxCustom.nix {};
      nixopsScripts = callPackage ./nix/nixopsScripts.nix {nixops_unstable_minimal = nixpkgs2405.nixops_unstable_minimal;};
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
      curlWithGnuTls
      loadtest
    ] ++ nixopsScripts;
  shellHook = ''
    export HISTFILE=.history
  '';
}
