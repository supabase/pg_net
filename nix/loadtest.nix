{ writeShellScriptBin, psrecord, writers, python3Packages } :

let
  psrecordToMd =
    writers.writePython3 "psrecord-to-md"
      {
        libraries = [ python3Packages.pandas python3Packages.tabulate ];
      }
      ''
        import sys
        import pandas as pd
        import re

        HEADER_SPLIT = re.compile(r"\s{2,}")

        raw_lines = sys.stdin.read().splitlines()

        header_line = next(
            (line for line in raw_lines if line.lstrip().startswith("#")), None
        )
        if header_line is None:
            sys.exit("Error: no header line found in input.")

        columns = HEADER_SPLIT.split(header_line.lstrip("#").strip())

        data_lines = [
            line.strip()
            for line in raw_lines
            if line.strip() and not line.lstrip().startswith("#")
        ]

        data_rows = [HEADER_SPLIT.split(line) for line in data_lines]

        df = pd.DataFrame(data_rows, columns=columns, dtype=str)

        df.to_markdown(sys.stdout, index=False, tablefmt="github")
      '';

  csvToMd =
    writers.writePython3 "csv-to-md"
      {
        libraries = [ python3Packages.pandas python3Packages.tabulate ];
      }
      ''
        import sys
        import pandas as pd

        pd.read_csv(sys.stdin) \
          .fillna("") \
          .convert_dtypes() \
          .to_markdown(sys.stdout, index=False, floatfmt='.0f')
      '';

in

writeShellScriptBin "net-loadtest" ''
  set -euo pipefail

  reqs=""
  batch_size_opt=""

  load_dir=test/load
  mkdir -p $load_dir
  echo "*" >> $load_dir/.gitignore

  record_result=$load_dir/psrecord.md
  query_result=$load_dir/query_out.md

  query_csv=$load_dir/query.csv
  record_log=$load_dir/psrecord.log

  if [ -n "''${1:-}" ]; then
    reqs="$1"
  fi

  if [ -n "''${2:-}" ]; then
    batch_size_opt="-c pg_net.batch_size=$2"
  fi

  net-with-nginx xpg --options "-c log_min_messages=WARNING $batch_size_opt" \
    psql -c "call wait_for_many_gets($reqs)" -c "\pset format csv" -c "\o $query_csv" -c "select * from run" > /dev/null &

  # wait for process to start so we can capture it with psrecord
  sleep 2

  ${psrecord}/bin/psrecord $(cat build-17/bgworker.pid) --interval 1 --log "$record_log" > /dev/null

  echo -e "## Loadtest results\n"
  cat $query_csv  | ${csvToMd}

  echo -e "\n\n## Loadtest elapsed seconds vs CPU/MEM\n"
  cat $record_log | ${psrecordToMd}
''
