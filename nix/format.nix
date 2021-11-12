{ clang-tools, git, writeShellScriptBin } :
rec {
  do =
    writeShellScriptBin "net-format" ''
      ${clang-tools}/bin/clang-format -i src/*
    '';
  doCheck =
    writeShellScriptBin "net-check-format" ''
      ${do}/bin/net-format

      ${git}/bin/git diff-index --exit-code HEAD -- '*.c'
    '';
}
