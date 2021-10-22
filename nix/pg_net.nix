{ stdenv, postgresql, curl, libuv }:

stdenv.mkDerivation {
  name = "pg_net";

  buildInputs = [ postgresql curl libuv ];

  src = ../.;

  installPhase = ''
    mkdir -p $out/bin
    install -D pg_net.so -t $out/lib

    install -D -t $out/share/postgresql/extension sql/*.sql
    install -D -t $out/share/postgresql/extension pg_net.control
  '';
}
