{ lib, fetchFromGitHub, nginx, nginxModules, writeShellScriptBin } :

let
  ngx_pathological = rec {
    name = "ngx_pathological";
    version = "0.1";
    src = fetchFromGitHub {
      owner  = "steve-chavez";
      repo   = name;
      rev    = "5eae52b15b0785765c5de17ede774f04cd60729d";
      sha256 = "sha256-oDvEZ2OVnM8lePYBUkQa294FLcLnxYMpE40S4XmqdBY=";
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
