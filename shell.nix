with import (builtins.fetchTarball {
    name = "25.05";
    url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/25.05.tar.gz";
    sha256 = "sha256:1915r28xc4znrh2vf4rrjnxldw2imysz819gzhk9qlrkqanmfsxd";
}) {};
mkShell {
  buildInputs =
    let
      nginxCustom = callPackage ./nix/nginxCustom.nix {};
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
    ];
  shellHook = ''
    export HISTFILE=.history
  '';
}
