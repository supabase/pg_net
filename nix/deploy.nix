let
  region = "us-east-2";
  accessKeyId = "supabase-dev";
in {
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
        instanceType             = "t3a.nano";
        associatePublicIpAddress = true;
        keyPair                  = resources.ec2KeyPairs.netKP;
        subnetId                 = resources.vpcSubnets.netSubnet;
        securityGroupIds         = [resources.ec2SecurityGroups.netSecGroup.name];
      };
    };

    services.nginx.enable = true;
    networking.firewall.allowedTCPPorts = [ 80 ];
  };

  client = { config, pkgs, nodes, resources, ... }: {
    deployment = {
      targetEnv = "ec2";
      ec2 = {
        inherit region accessKeyId;
        instanceType             = "t3a.nano";
        associatePublicIpAddress = true;
        ebsInitialRootDiskSize   = 6;
        keyPair                  = resources.ec2KeyPairs.netKP;
        subnetId                 = resources.vpcSubnets.netSubnet;
        securityGroupIds         = [resources.ec2SecurityGroups.netSecGroup.name];
      };
    };

    services.postgresql = {
      enable = true;
      package = pkgs.postgresql_12.withPackages (p: [
        (pkgs.callPackage ./pg_net.nix { postgresql = pkgs.postgresql_12;})
      ]);
      authentication = pkgs.lib.mkOverride 10 ''
        local   all all trust
      '';
      settings = {
        shared_preload_libraries = "pg_net";
      };
    };

    networking.hosts = {
      "${nodes.server.config.networking.privateIPv4}" = [ "server" ];
    };
  };

}
