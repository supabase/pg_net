{
  description = "Development shell for pg_net";

  # To skip hours of building and download packages instead, start using the
  # binary cache: https://github.com/supabase/postgres/blob/develop/nix/docs/binary-cache.md

  inputs = {
    # 2025-11-13
    nixpkgs.url = "github:NixOS/nixpkgs/91c9a64ce2a84e648d0cf9671274bb9c2fb9ba60";
    xpg = {
      url = "github:steve-chavez/xpg/v2.4.0";
    };
  };

  outputs = { self, nixpkgs, xpg }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          xpgPkgs = xpg.packages.${system};
        in
        {
          default = import ./shell.nix {
            inherit pkgs xpgPkgs;
          };
        });
    };
}
