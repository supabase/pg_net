{ fetchFromGitHub, lib } :
let
  dep = fetchFromGitHub {
    owner  = "steve-chavez";
    repo   = "xpg";
    rev    = "v1.3.1";
    sha256 = "sha256-JuTjM/94TAcjYLxZJa/GDfZG/7FvkfG5FKpCTkuZ48k=";
  };
  xpg = import dep;
in
xpg
