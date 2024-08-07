{ nginx, nginxModules, writeShellScriptBin } :

let
  customNginx = nginx.override {
    modules = [
      nginxModules.echo
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
