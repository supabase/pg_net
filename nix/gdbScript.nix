{ gdb, writeText, writeShellScriptBin } :

let
  file = writeText "gdbconf" ''
    # Do this so we can do `backtrace` once a segfault occurs. Otherwise once SIGSEGV is received the bgworker will quit and we can't backtrace.
    handle SIGSEGV stop nopass
  '';
  script = ''
    ${gdb}/bin/gdb -x ${file} "$@"
  '';
in
writeShellScriptBin "with-gdb" script
