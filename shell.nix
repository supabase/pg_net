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
, pgVersion ? null
, cassert ? true
}:
let
  nginxCustom = pkgs.callPackage ./nix/nginxCustom.nix {};
  loadtest = pkgs.callPackage ./nix/loadtest.nix {};
  pythonDeps = with pkgs.python3Packages; [
    pytest
    psycopg
    sqlalchemy
  ];
  style =
    pkgs.writeShellScriptBin "net-style" ''
      ${pkgs.clang-tools}/bin/clang-format -i src/*
      ${pkgs.ruff}/bin/ruff format
      sql_files=$(ls sql/)
      for sql_file in $sql_files; do
	      ${pkgs.python313Packages.pglast}/bin/pgpp sql/''$sql_file > sql/tmp_''$sql_file;
	      mv sql/tmp_''$sql_file sql/''$sql_file;
      done;
    '';
  styleCheck =
    pkgs.writeShellScriptBin "net-style-check" ''
      ${pkgs.clang-tools}/bin/clang-format -i src/*
      ${pkgs.git}/bin/git diff-index --exit-code HEAD -- '*.c'
      ${pkgs.ruff}/bin/ruff check
      sql_files=$(ls sql/)
      for sql_file in $sql_files; do
	      ${pkgs.python313Packages.pglast}/bin/pgpp sql/''$sql_file > sql/tmp_''$sql_file;
	      if cmp -s sql/tmp_''$sql_file sql/''$sql_file; then
			rm sql/tmp_''$sql_file
	      else
			rm sql/tmp_''$sql_file
			echo "diff found in ''$sql_file"
			exit 1
	      fi
      done;
    '';
in
pkgs.mkShell {
  buildInputs =
    [
      (if pgVersion == null then xpgPkgs.xpg else xpgPkgs.xpg.forVersions { versions = [ pgVersion ]; inherit cassert; })
      pythonDeps
      nginxCustom.nginxScript
      pkgs.curlWithGnuTls
      loadtest
      style
      styleCheck
      pkgs.ruff
      pkgs.python313Packages.pglast
    ];
  shellHook = ''
    export HISTFILE=.history
  '';
}
