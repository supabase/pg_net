{ clang-tools, writeShellScriptBin } :
writeShellScriptBin "net-format" ''
  ${clang-tools}/bin/clang-format -i src/*
''
