{ fetchFromGitHub, lib } :
let
  dep = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "xpg";
    rev    = "v1.6.0";
    sha256 = "sha256-NsdAmsYIRH/DWIZp93AHGYdPiJOztUIUSYcPikeebvw=";
  };
  xpg = import dep;
in
xpg
