let
  region = "us-east-2";
  accessKeyId = "default";
in {
  network.storage.legacy = {
    databasefile = ".deployments.nixops";
  };

  network.description = "pg_net load testing setup";

  resources = {
    ec2KeyPairs.netKP = { inherit region accessKeyId; };
    vpc.netVpc = {
      inherit region accessKeyId;
      enableDnsSupport = true;
      enableDnsHostnames = true;
      cidrBlock = "10.0.0.0/24";
    };
    vpcSubnets.netSubnet = {resources, ...}: {
      inherit region accessKeyId;
      zone = "${region}a";
      vpcId = resources.vpc.netVpc;
      cidrBlock = "10.0.0.0/24";
      mapPublicIpOnLaunch = true;
    };
    vpcInternetGateways.netIG = { resources, ... }: {
      inherit region accessKeyId;
      vpcId = resources.vpc.netVpc;
    };
    vpcRouteTables.netRT = { resources, ... }: {
      inherit region accessKeyId;
      vpcId = resources.vpc.netVpc;
    };
    vpcRoutes.netIGRoute = { resources, ... }: {
      inherit region accessKeyId;
      routeTableId = resources.vpcRouteTables.netRT;
      destinationCidrBlock = "0.0.0.0/0";
      gatewayId = resources.vpcInternetGateways.netIG;
    };
    vpcRouteTableAssociations.netTblAssoc = { resources, ... }: {
      inherit region accessKeyId;
      subnetId = resources.vpcSubnets.netSubnet;
      routeTableId = resources.vpcRouteTables.netRT;
    };
    ec2SecurityGroups.netSecGroup = {resources, ...}: {
      inherit region accessKeyId;
      vpcId = resources.vpc.netVpc;
      rules = [
        { fromPort = 80;  toPort = 80;    sourceIp = "0.0.0.0/0"; }
        { fromPort = 22;  toPort = 22;    sourceIp = "0.0.0.0/0"; }
        { fromPort = 0;   toPort = 65535; sourceIp = resources.vpcSubnets.netSubnet.cidrBlock; }
      ];
    };
  };

  server = { config, pkgs, resources, ... }: {
    deployment = {
      targetEnv = "ec2";
      ec2 = {
        inherit region accessKeyId;
        instanceType             = "t3a.micro";
        associatePublicIpAddress = true;
        keyPair                  = resources.ec2KeyPairs.netKP;
        subnetId                 = resources.vpcSubnets.netSubnet;
        securityGroupIds         = [resources.ec2SecurityGroups.netSecGroup.name];
      };
    };

    services.nginx = {
      enable = true;
      package = (pkgs.callPackage ./nginxCustom.nix {}).customNginx;
      config = ''
        worker_processes auto;
        events {
          worker_connections 1024;
        }
        http {
          server {
            listen 0.0.0.0:80 ;
            listen [::]:80 ;
            server_name localhost;
            ${builtins.readFile nginx/conf/custom.conf}
          }
        }
      '';
    };
    networking.firewall.allowedTCPPorts = [ 80 ];
  };

  client = { config, pkgs, nodes, resources, ... }: {
    deployment = {
      targetEnv = "ec2";
      ec2 = {
        inherit region accessKeyId;
        instanceType             = "t3a.micro";
        associatePublicIpAddress = true;
        ebsInitialRootDiskSize   = 6;
        keyPair                  = resources.ec2KeyPairs.netKP;
        subnetId                 = resources.vpcSubnets.netSubnet;
        securityGroupIds         = [resources.ec2SecurityGroups.netSecGroup.name];
      };
    };

    services.postgresql = {
      enable = true;
      package = pkgs.postgresql_15.withPackages (p: [
        (pkgs.callPackage ./pg_net.nix { postgresql = pkgs.postgresql_15;})
      ]);
      authentication = pkgs.lib.mkOverride 10 ''
        local   all all trust
      '';
      settings = {
        shared_preload_libraries = "pg_net";
      };
      initialScript = pkgs.writeText "init-sql-script" ''
        create extension pg_net;
        ${builtins.readFile ../test/loadtest/loadtest.sql}
      '';
    };

    services.journald.rateLimitBurst = 0;
    services.journald.rateLimitInterval = "0";

    networking.hosts = {
      "${nodes.server.config.networking.privateIPv4}" = [ "server" ];
    };

    environment.systemPackages = [
      pkgs.bcc
      pkgs.pgmetrics
      pkgs.pg_activity
      pkgs.htop
      pkgs.vegeta
      (
        pkgs.writeShellScriptBin "vegeta-bench" ''
          set -euo pipefail

          # rate=0 means maximum rate subject to max-workers
          echo "GET http://server/pathological?status=200" | vegeta attack -rate=0 -duration=1s -max-workers=1 | tee results.bin | vegeta report
        ''
      )
      (
        pkgs.writeShellScriptBin "vegeta-bench-max-requests" ''
          set -euo pipefail

          # rate=0 means maximum rate subject to max-workers
          echo "GET http://server/pathological?status=200" | vegeta attack -rate=0 -duration=10s -max-workers=50 | tee results.bin | vegeta report
        ''
      )
      (
        pkgs.writeShellScriptBin "psql-net-bench" ''
          set -euo pipefail

          psql -U postgres -c "TRUNCATE net._http_response; TRUNCATE net.http_request_queue;"
          psql -U postgres -c "alter system set pg_net.batch_size to 32000;" # this just a high number
          psql -U postgres -c "select net.worker_restart();"
          psql -U postgres -c "truncate net._http_response;"
          psql -U postgres -c "select net.http_get('http://server/pathological?status=200') from generate_series(1, 400);" > /dev/null
          sleep 2
          psql -U postgres -c "select * from pg_net_stats;"
          psql -U postgres -c "alter system reset pg_net.batch_size;"
          psql -U postgres -c "select net.worker_restart();"
        ''
      )
      (
        pkgs.writeShellScriptBin "psql-net-many-gets" ''
          set -euo pipefail

          psql -U postgres -c "call wait_for_many_gets(url:='$1');"
        ''
      )
    ];
  };

}
