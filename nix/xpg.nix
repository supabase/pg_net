{ fetchFromGitHub, lib } :
let
  dep = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "xpg";
    rev    = "v1.3.2";
    sha256 = "sha256-ooYqMOQD9y+/87wBd33Mvbpsx+FwEMdZoibGRM4gvBk=";
  };
  xpg = import dep;
in
xpg
