{ openresty, writeShellScriptBin } :

let
  script = ''
    export PATH=${openresty}/bin:"$PATH"

    openresty -p nix/nginx &

    "$@"
  '';
in
writeShellScriptBin "net-with-nginx" script
