{ lib, stdenv, fetchFromGitHub, postgresql }:

stdenv.mkDerivation rec {
  pname = "pg_stat_monitor";
  version = "2.1.1";

  buildInputs = [ postgresql ];

  src = fetchFromGitHub {
    owner = "percona";
    repo = pname;
    rev = "refs/tags/${version}";
    hash = "sha256-c50l6XpkF5lp8TQd9TFqnms3nc8KAAa+dV/wVjw3Zao=";
  };

  makeFlags = [ "USE_PGXS=1" ];

  installPhase = ''
    install -D -t $out *${postgresql.dlSuffix}
    install -D -t $out *.sql
    install -D -t $out *.control
  '';

  meta = with lib; {
    description = "Query Performance Monitoring Tool for PostgreSQL";
    homepage = "https://github.com/percona/${pname}";
    maintainers = with maintainers; [ samrose ];
    platforms = postgresql.meta.platforms;
    license = licenses.postgresql;
    broken = lib.versionOlder postgresql.version "15";
  };
}
