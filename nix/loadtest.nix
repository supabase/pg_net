{ writeShellScriptBin, psrecord, writers, python3Packages } :

let
  toMarkdown =
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
in

writeShellScriptBin "net-loadtest" ''
  set -euo pipefail

  reqs=""
  batch_size="200"

  if [ -n "''${1:-}" ]; then
    reqs="$1"
  fi

  if [ -n "''${2:-}" ]; then
    batch_size="$2"
  fi

  net-with-nginx xpg --options "-c log_min_messages=WARNING -c pg_net.batch_size=$batch_size" psql -c "call wait_for_many_gets($reqs)" > /dev/null &

  # wait for process to start so we can capture it with psrecord
  sleep 2

  record_log=psrecord.log
  record_result=psrecord.md

  ${psrecord}/bin/psrecord $(cat build-17/bgworker.pid) --interval 1 --log "$record_log" > /dev/null

  cat $record_log | ${toMarkdown} > $record_result

  echo "generated $record_result"
''
