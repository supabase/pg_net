{ fetchFromGitHub, lib } :
let
  dep = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "xpg";
    rev    = "v1.8.0";
    sha256 = "sha256-ltS2bprvzrmaBjzMmIiSdJh5P3gBV/blzFpYazevv8g=";
  };
  xpg = import dep;
in
xpg
