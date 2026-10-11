#!/usr/bin/env bash
#
# Reproduces the pg_net worker segfault caused by net_on_exit() cleaning up the
# curl handle / epoll fd that the *previous* worker process left in shared memory.
#
# How it triggers: a worker that is restarted while pg_net.database_name points to a
# missing database dies in BackgroundWorkerInitializeConnection(), i.e. before it
# created its own curl handle, and its exit hook then calls curl_multi_cleanup() on
# the dangling pointer stored by the previous worker. On Linux this is a SIGSEGV
# which makes the postmaster restart the whole cluster.
#
# Runs on the host (e.g. macOS with OrbStack/Docker): the given git revisions are
# exported with `git archive`, then built and tested in a throwaway postgres:17
# (Debian) container. Needs docker and network access (apt).
#
# usage: ./repro_stale_curl_handle.sh [git-ref ...]     (default: HEAD)
#   RUNS=N      iterations per ref (default 5)
#   PG_MAJOR=N  postgres major version, 14-17 images (default 17)
#
# e.g. compare the code before and after the fix:
#   ./repro_stale_curl_handle.sh <commit-before-fix> <commit-with-fix>
# Expected: "CRASH" for the revision without the fix, "no crash" for the one with it.

set -euo pipefail

RUNS=${RUNS:-5}
PG_MAJOR=${PG_MAJOR:-17}
REFS=("$@")
[ ${#REFS[@]} -eq 0 ] && REFS=(HEAD)

read -r -d '' INNER <<'EOF' || true
set -u
export DEBIAN_FRONTEND=noninteractive
PGV=$1; RUNS=$2
B=/usr/lib/postgresql/$PGV/bin
apt-get update -qq >/dev/null
apt-get install -y -qq build-essential postgresql-server-dev-$PGV libcurl4-openssl-dev libicu-dev >/dev/null 2>&1
cp -r /src /build && cd /build
if ! make -s PG_CONFIG=$B/pg_config >/tmp/make.log 2>&1 || ! make -s install PG_CONFIG=$B/pg_config >>/tmp/make.log 2>&1; then
  cat /tmp/make.log; exit 2
fi
cat > /tmp/run.sh <<'EOS'
B=/usr/lib/postgresql/$1/bin
crashes=0
for i in $(seq 1 $2); do
  D=/tmp/pgd$i; rm -rf $D; $B/initdb -D $D >/dev/null 2>&1
  printf "shared_preload_libraries='pg_net'\nport=5444\nunix_socket_directories='/tmp'\n" >> $D/postgresql.conf
  $B/pg_ctl -D $D -l $D/log -w start >/dev/null
  P="$B/psql -h /tmp -p 5444 -X -q -At postgres"
  $P -c "create extension pg_net" >/dev/null
  $P -c "select net.wait_until_running()" >/dev/null
  # point the worker at a missing database, then restart it: the first worker exits
  # cleanly (leaving its freed curl handle in shared memory) and every replacement
  # dies before initializing curl
  $P -c "alter system set pg_net.database_name='does_not_exist'" -c "select pg_reload_conf()" >/dev/null
  $P -c "select net.worker_restart()" >/dev/null
  sleep 6
  deaths=$(grep -c 'does not exist' $D/log)
  if grep -q "terminated by signal" $D/log; then
    crashes=$((crashes+1)); echo "  run $i: CRASH ($deaths early worker deaths)"
    # stop at the end of crash recovery: shared memory is reset after it, so later lines are just noise
    grep -E "REPRO|does not exist|terminated by signal|terminating any other|reinitializing" $D/log | awk '{print} /reinitializing/ {exit}' | sed 's/^/      /'
  else
    echo "  run $i: no crash ($deaths early worker deaths)"
  fi
  $B/pg_ctl -D $D -m immediate stop >/dev/null
done
echo "  => $crashes/$2 runs crashed"
EOS
chmod 755 /tmp/run.sh
su postgres -c "/tmp/run.sh $PGV $RUNS"
EOF

for ref in "${REFS[@]}"; do
  src=$(mktemp -d)
  git archive "$ref" | tar -x -C "$src"
  echo "### $ref ($(git log -1 --format=%h\ %s "$ref")) on postgres:$PG_MAJOR, $RUNS runs"
  docker run --rm -v "$src":/src:ro "postgres:$PG_MAJOR" bash -c "$INNER" _ "$PG_MAJOR" "$RUNS"
  rm -rf "$src"
done
