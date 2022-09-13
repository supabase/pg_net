{ openresty, writeShellScriptBin } :

let
  script = ''
    export PATH=${openresty}/bin:"$PATH"

    trap 'jobs -p | xargs kill -9' sigint sigterm exit

    openresty -p nix/nginx &

    "$@"
  '';
in
writeShellScriptBin "net-with-nginx" script
