{ fetchFromGitHub, lib } :
let
  dep = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "xpg";
    rev    = "v1.3.3";
    sha256 = "sha256-N0/jp+tOWFz72Z6ZK2oA0M6e5F1W3SPhgk5G/0PbBso=";
  };
  xpg = import dep;
in
xpg
