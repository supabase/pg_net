{
  description = "Development shell for pg_net";

  inputs = {
    # 2025-11-13
    nixpkgs.url = "github:NixOS/nixpkgs/91c9a64ce2a84e648d0cf9671274bb9c2fb9ba60";

    xpg = {
      url = "github:supabase/xpg/v2.7.1";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      xpg,
    }:
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
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
          xpgPkgs = xpg.packages.${system};
        in
        {
          default = import ./shell.nix {
            pkgs = pkgs;
            xpgPkgs = xpgPkgs;
            curl = pkgs.curlWithGnuTls;
          };
          curl_7_76_1 = import ./shell.nix {
            pkgs = pkgs;
            xpgPkgs = xpgPkgs;
            curl = pkgs.curlWithGnuTls.overrideAttrs (old: {
              version = "7.76.1";
              configureFlags = old.configureFlags ++ [ "--enable-ipv6" ];
              src = pkgs.fetchurl {
                urls = [
                  "https://github.com/curl/curl/releases/download/curl-7_76_1/curl-7.76.1.tar.xz"
                ];
                hash = "sha256-ZLtSiMOfCEDAfQd+MNkFLhy7n6bC3FJSOCTMhZ5nkUU=";
              };
            });
          };
        }
      );
    };
}
