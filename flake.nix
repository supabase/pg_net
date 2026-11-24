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

          # 7.86.0 introduced changes in error message
          curl_7_85_0 = import ./shell.nix {
            pkgs = pkgs;
            xpgPkgs = xpgPkgs;
            curl = (pkgs.curlWithGnuTls.override { http3Support = false; }).overrideAttrs (old: {
              version = "7.85.0";
              configureFlags = old.configureFlags ++ [ "--enable-ipv6" ];

              src = pkgs.fetchurl {
                urls = [
                  "https://github.com/curl/curl/releases/download/curl-7_85_0/curl-7.85.0.tar.xz"
                ];
                hash = "sha256-iLVKbUuaSMtNhzxwVty6mX3dW3vlotU3pKy1XCCwS+Y=";
              };
            });
          };

          # 7.84.0 introduced changes in header behaviour
          curl_7_83_1 = import ./shell.nix {
            pkgs = pkgs;
            xpgPkgs = xpgPkgs;
            curl = (pkgs.curlWithGnuTls.override { http3Support = false; }).overrideAttrs (old: {
              version = "7.83.1";
              configureFlags = old.configureFlags ++ [ "--enable-ipv6" ];

              src = pkgs.fetchurl {
                urls = [
                  "https://github.com/curl/curl/releases/download/curl-7_83_1/curl-7.83.1.tar.xz"
                ];
                hash = "sha256-LLnCNW5yY6EnL9FDXvfN6/LNIUAOwoewaDlt63BcIsQ=";
              };
            });
          };

          # minimum support curl version
          curl_7_83_0 = import ./shell.nix {
            pkgs = pkgs;
            xpgPkgs = xpgPkgs;
            curl = (pkgs.curlWithGnuTls.override { http3Support = false; }).overrideAttrs (old: {
              version = "7.83.0";
              configureFlags = old.configureFlags ++ [ "--enable-ipv6" ];

              src = pkgs.fetchurl {
                urls = [
                  "https://github.com/curl/curl/releases/download/curl-7_83_0/curl-7.83.0.tar.xz"
                ];
                hash = "sha256-u/8Oa1BH53Pzw7CE2AVGzBvk41TAnkGcLQ72EWJTURo=";
              };
            });
          };
        }
      );
    };
}
