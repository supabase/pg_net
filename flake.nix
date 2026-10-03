{
  description = "Development shell for pg_net";

  inputs = {
    # 2026-05-30 : 25.05
    nixpkgs.url = "github:NixOS/nixpkgs/8c50a710ddca43d7a530fb805ad55bde8d0141c5";
    xpg = {
      url = "github:supabase/xpg/v2.5.0";
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
