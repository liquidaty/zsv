#!/bin/sh
# Benchmark: compare default vs fast parser engine
#
# Usage: sh app/benchmark/bench_fast_parser.sh [iterations]
#
# Requires: app/benchmark/worldcitiespop.csv

ITERATIONS=${1:-5}
CSV=app/benchmark/worldcitiespop.csv
COUNT=./build/Darwin/rel/gcc-14/bin/zsv_count
SELECT=./build/Darwin/rel/gcc-14/bin/zsv_select

if [ ! -f "$CSV" ]; then
  echo "Error: $CSV not found" >&2
  exit 1
fi
if [ ! -x "$COUNT" ]; then
  echo "Error: $COUNT not found (run make -C src install && make -C app)" >&2
  exit 1
fi
if [ ! -x "$SELECT" ]; then
  echo "Error: $SELECT not found (run make -C src install && make -C app)" >&2
  exit 1
fi

bench() {
  label="$1"
  shift
  total=0
  for i in $(seq 1 "$ITERATIONS"); do
    t=$( { /usr/bin/time "$@" > /dev/null; } 2>&1 | awk '{
      # parse real time from /usr/bin/time output (macOS format: N.NN real)
      for(i=1;i<=NF;i++) if($i=="real") { print $(i-1); exit }
    }')
    if [ -z "$t" ]; then
      # try GNU time format (0m0.000s)
      t=$( { /usr/bin/time "$@" > /dev/null; } 2>&1 | sed -n 's/.*real[[:space:]]*0m\([0-9.]*\)s.*/\1/p' )
    fi
    total=$(echo "$total + $t" | bc)
  done
  avg=$(echo "scale=4; $total / $ITERATIONS" | bc)
  printf "  %-40s avg %.4fs (%d runs)\n" "$label" "$avg" "$ITERATIONS"
}

echo "=== count (--no-quote) ==="
bench "default" "$COUNT" --no-quote --parser default "$CSV"
bench "fast"    "$COUNT" --no-quote --parser fast    "$CSV"

echo ""
echo "=== select --no-trim -n -- 1 2 3 4 6 5 (--no-quote) ==="
bench "default" "$SELECT" --no-quote --no-trim -n --parser default -- 1 2 3 4 6 5 < "$CSV"
bench "fast"    "$SELECT" --no-quote --no-trim -n --parser fast    -- 1 2 3 4 6 5 < "$CSV"
