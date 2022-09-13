{ mitmproxy, writeShellScriptBin } :

let
  script = ''
    export PATH=${mitmproxy}/bin:"$PATH"

    trap 'jobs -p | xargs kill -9' sigint sigterm exit

    pathod &

    "$@"
  '';
in
writeShellScriptBin "net-with-pathod" script
