{ fetchFromGitHub } :
let
  nxpg = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "nxpg";
    rev    = "v1.1";
    sha256 = "sha256-R0Z2vDjFtkrFgetL1L/N1iWN0Bh+TWBZ7VZLDARJ3pY=";
  };
  script = import nxpg;
in
script
