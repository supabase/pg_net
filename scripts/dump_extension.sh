leaf_pid() {
  local pid=$1 child
  while child=$(pgrep -P "$pid" | head -n1); [ -n "$child" ]; do
    pid=$child
  done
  echo "$pid"
}

xpg psql \
  --file=./scripts/drop_all_extension_objects.sql \
  --command='SELECT pg_sleep(100)' > /dev/null &

xpg_pid=$!
sleep 1
psql_pid=$(leaf_pid $xpg_pid)

export PGHOST=$(ps eww -p "${psql_pid}" | tr ' ' '\n' | grep "^PGHOST=" | awk -F '=' '{ print $2 }')

pg_dump \
	--schema=net \
	--user=postgres \
	--restrict-key=0000 => pg_net.sql
kill $xpg_pid

