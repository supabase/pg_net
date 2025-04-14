{ fetchFromGitHub, lib } :
let
  dep = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "xpg";
    rev    = "v1.3.0";
    sha256 = "sha256-jDELiBbnCpRXIpod7msnhMfGcrW0pR3snDQ5T81nO0I=";
  };
  xpg = import dep;
in
xpg
