create database pre_existing;
create role pre_existing nosuperuser login;
create extension pg_net;
\ir ../nix/bench.sql
