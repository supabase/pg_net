{ gdb, writeText, writeShellScriptBin, pidFileName } :

let
  file = writeText "gdbconf" ''
    # Do this so we can do `backtrace` once a segfault occurs. Otherwise once SIGSEGV is received the bgworker will quit and we can't backtrace.
    handle SIGSEGV stop nopass
  '';
  script = ''
    set -euo pipefail

    if [ ! -e ${pidFileName} ]
    then
      echo "The pg_net worker is not started. Exiting."
      exit 1
    fi
    pid=$(cat ${pidFileName})
    ${gdb}/bin/gdb -x ${file} -p ''$pid "$@"
  '';
in
writeShellScriptBin "net-with-gdb" script
