let
  flakeLock = builtins.fromJSON (builtins.readFile ./flake.lock);
  nixpkgsLock = flakeLock.nodes.nixpkgs.locked;
  xpgLock = flakeLock.nodes.xpg.locked;
in
{ pkgs ?
  import (builtins.fetchTarball {
    name = nixpkgsLock.rev;
    url = "https://github.com/${nixpkgsLock.owner}/${nixpkgsLock.repo}/archive/${nixpkgsLock.rev}.tar.gz";
    sha256 = nixpkgsLock.narHash;
  }) { }
, xpgPkgs ?
  import (pkgs.fetchFromGitHub {
    inherit (xpgLock) owner repo rev;
    sha256 = xpgLock.narHash;
  })
}:
let
  nginxCustom = pkgs.callPackage ./nix/nginxCustom.nix {};
  loadtest = pkgs.callPackage ./nix/loadtest.nix {};
  pythonDeps = with pkgs.python3Packages; [
    pytest
    psycopg2
    sqlalchemy
  ];
  style =
    pkgs.writeShellScriptBin "net-style" ''
      ${pkgs.clang-tools}/bin/clang-format -i src/*
    '';
  styleCheck =
    pkgs.writeShellScriptBin "net-style-check" ''
      ${pkgs.clang-tools}/bin/clang-format -i src/*
      ${pkgs.git}/bin/git diff-index --exit-code HEAD -- '*.c'
    '';
in
pkgs.mkShell {
  buildInputs =
    [
      xpgPkgs.xpg
      pythonDeps
      nginxCustom.nginxScript
      pkgs.curlWithGnuTls
      loadtest
      style
      styleCheck
    ];
  shellHook = ''
    export HISTFILE=.history
  '';
}
