{ openresty, writeShellScriptBin } :

let
  script = ''
    set -euo pipefail

    export PATH=${openresty}/bin:"$PATH"

    trap 'killall openresty' sigint sigterm exit

    openresty -p nix/nginx &

    "$@"
  '';
in
writeShellScriptBin "net-with-nginx" script
