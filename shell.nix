with import (builtins.fetchTarball {
  name = "2025-11-13";
  url = "https://github.com/NixOS/nixpkgs/archive/91c9a64ce2a84e648d0cf9671274bb9c2fb9ba60.tar.gz";
  sha256 = "sha256:19myp93spfsf5x62k6ncan7020bmbn80kj4ywcykqhb9c3q8fdr1";
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
      style =
        writeShellScriptBin "net-style" ''
          ${clang-tools}/bin/clang-format -i src/*
        '';
      styleCheck =
        writeShellScriptBin "net-style-check" ''
          ${clang-tools}/bin/clang-format -i src/*
          ${git}/bin/git diff-index --exit-code HEAD -- '*.c'
        '';
    in
    [
      xpg.xpg
      pythonDeps
      nginxCustom.nginxScript
      curlWithGnuTls
      loadtest
      style
      styleCheck
    ];
  shellHook = ''
    export HISTFILE=.history
  '';
}
