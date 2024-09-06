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

        alter system set pg_net.batch_size to 32000;

        select net.worker_restart();

        create view pg_net_stats as
        select
          count(*) filter (where error_msg is null) as request_successes,
          count(*) filter (where error_msg is not null) as request_failures,
          (select error_msg from net._http_response where error_msg is not null order by id desc limit 1) as last_failure_error
        from net._http_response;
      '';
    };

    networking.hosts = {
      "${nodes.server.config.networking.privateIPv4}" = [ "server" ];
    };

    environment.systemPackages = [
      pkgs.vegeta
      (
        pkgs.writeShellScriptBin "vegeta-bench" ''
          # rate=0 means maximum rate subject to max-workers
          echo "GET http://server/pathological?status=200" | vegeta attack -rate=0 -duration=1s -max-workers=1 | tee results.bin | vegeta report
        ''
      )
      (
        pkgs.writeShellScriptBin "vegeta-bench-max-requests" ''
          # rate=0 means maximum rate subject to max-workers
          echo "GET http://server/pathological?status=200" | vegeta attack -rate=0 -duration=10s -max-workers=50 | tee results.bin | vegeta report
        ''
      )
      (
        pkgs.writeShellScriptBin "net-bench" ''
          psql -U postgres -c "truncate net._http_response;"
          psql -U postgres -c "select net.http_get('http://server/pathological?status=200') from generate_series(1, 400);" > /dev/null
          sleep 2
          psql -U postgres -c "select * from pg_net_stats;"
        ''
      )
    ];
  };

}
