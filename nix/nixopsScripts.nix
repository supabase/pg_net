{ nixops_unstable_minimal, writeShellScriptBin } :

let
  nixops = nixops_unstable_minimal.withPlugins (ps: [ ps.nixops-aws ]);
  nixopsBin = "${nixops}/bin/nixops";
  nixopsDeploy =
    writeShellScriptBin "net-cloud-deploy"
      ''
        set -euo pipefail

        cd nix

        set +e && ${nixopsBin} info -d pg_net > /dev/null 2> /dev/null
        info=$? && set -e

        if test $info -eq 1
        then
          echo "Creating deployment..."
          ${nixopsBin} create -d pg_net
        fi

        ${nixopsBin} deploy -k -d pg_net --allow-reboot --confirm
      '';
  nixopsSSH =
    writeShellScriptBin ("net-cloud-ssh")
      ''
        set -euo pipefail

        cd nix

        ${nixopsBin} ssh -d pg_net client
      '';
  nixopsSSHServer =
    writeShellScriptBin ("net-cloud-ssh-server")
      ''
        set -euo pipefail

        cd nix

        ${nixopsBin} ssh -d pg_net server
      '';
  nixopsReproTimeouts =
    writeShellScriptBin ("net-cloud-reproduce-timeouts")
      ''
        set -euo pipefail

        cd nix

        ${nixopsBin} ssh -d pg_net client psql-reproduce-timeouts
      '';
  nixopsDestroy =
    writeShellScriptBin ("net-cloud-destroy")
      ''
        set -euo pipefail

        cd nix

        ${nixopsBin} destroy -d pg_net --confirm

        ${nixopsBin} delete -d pg_net
      '';
  nixopsInfo =
    writeShellScriptBin ("net-cloud-info")
      ''
        set -euo pipefail

        cd nix

        ${nixopsBin} info
      '';
in
[
  nixopsDeploy
  nixopsSSH
  nixopsSSHServer
  nixopsReproTimeouts
  nixopsDestroy
  nixopsInfo
]
