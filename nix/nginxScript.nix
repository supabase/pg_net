{ lib, fetchFromGitHub, nginx, nginxModules, writeShellScriptBin } :

let
  ngx_pathological = rec {
    name = "ngx_pathological";
    version = "0.1";
    src = fetchFromGitHub {
      owner  = "steve-chavez";
      repo   = name;
      rev    = "46a6d48910b482cf82ecd8a3fce448498cc9f886";
      sha256 = "sha256-unPSFxbuvv5chyPKDRaKvxuekz3tPmhHiVzZzX1D4lk=";
    };
    meta = with lib; {
      license = with licenses; [ mit ];
    };
  };
  customNginx = nginx.override {
    modules = [
      nginxModules.echo
      ngx_pathological
    ];
  };
  script = ''
    set -euo pipefail

    export PATH=${customNginx}/bin:"$PATH"

    trap 'killall nginx' sigint sigterm exit

    nginx -p nix/nginx -e stderr &

    "$@"
  '';
in
writeShellScriptBin "net-with-nginx" script
