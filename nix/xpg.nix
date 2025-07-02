{ fetchFromGitHub, lib } :
let
  dep = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "xpg";
    rev    = "v1.4.1";
    sha256 = "sha256-OI9g78KbguLh+ynOnRmnMM4lVOgNRAWkiI/YMmcMs+k=";
  };
  xpg = import dep;
in
xpg
