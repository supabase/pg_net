{
  lib,
  stdenv,
  fetchFromGitHub,
  postgresql,
}:

stdenv.mkDerivation rec {
  pname = "plpgsql-check";
  version = "2.10.12";

  src = fetchFromGitHub {
    owner = "okbob";
    repo = "plpgsql_check";
    rev = "v${version}";
    hash = "sha256-99TemcnEgbhSY1YhSrIdAlclgIzak+X2PEaC/QUJwic=";
  };

  buildInputs = [ postgresql ];

  installPhase = ''
    install -D -t $out *${postgresql.dlSuffix}
    install -D -t $out *.sql
    install -D -t $out *.control

    # Needed for postgres 18+ native implementation of extension_search_path
    mkdir $out/extension
    install -D -t $out/extension *.sql
    install -D -t $out/extension *.control
  '';

  meta = with lib; {
    description = "Linter tool for language PL/pgSQL";
    homepage = "https://github.com/okbob/plpgsql_check";
    changelog = "https://github.com/okbob/plpgsql_check/releases/tag/v${version}";
    platforms = postgresql.meta.platforms;
    license = licenses.mit;
    maintainers = [ maintainers.marsam ];
  };
}
