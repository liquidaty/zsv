#!/bin/sh
# Benchmark: compare default vs fast parser engine, plus xsv and xan
#
# Usage: sh app/benchmark/bench_fast_parser.sh [iterations]
#
# Requires: app/benchmark/worldcitiespop.csv

ITERATIONS=${1:-5}
CSV=app/benchmark/worldcitiespop.csv
COUNT=./build/Darwin/rel/gcc-14/bin/zsv_count
SELECT=./build/Darwin/rel/gcc-14/bin/zsv_select
XSV=$(which xsv 2>/dev/null)
XAN=$(which xan 2>/dev/null)

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

bench_stdin() {
  label="$1"
  shift
  total=0
  for i in $(seq 1 "$ITERATIONS"); do
    t=$( { /usr/bin/time "$@" < "$CSV" > /dev/null; } 2>&1 | awk '{
      for(i=1;i<=NF;i++) if($i=="real") { print $(i-1); exit }
    }')
    if [ -z "$t" ]; then
      t=$( { /usr/bin/time "$@" < "$CSV" > /dev/null; } 2>&1 | sed -n 's/.*real[[:space:]]*0m\([0-9.]*\)s.*/\1/p' )
    fi
    total=$(echo "$total + $t" | bc)
  done
  avg=$(echo "scale=4; $total / $ITERATIONS" | bc)
  printf "  %-40s avg %.4fs (%d runs)\n" "$label" "$avg" "$ITERATIONS"
}

echo "=== count ==="
bench "zsv (default)"  "$COUNT" --parser default "$CSV"
bench "zsv (fast)"     "$COUNT" --parser fast    "$CSV"
bench "zsv -q (default)" "$COUNT" --no-quote --parser default "$CSV"
bench "zsv -q (fast)"    "$COUNT" --no-quote --parser fast    "$CSV"
[ -n "$XSV" ] && bench "xsv" "$XSV" count "$CSV"
[ -n "$XAN" ] && bench "xan" "$XAN" count "$CSV"

echo ""
echo "=== select 6 columns ==="
bench_stdin "zsv (default)"  "$SELECT" --no-trim -n --parser default -- 1 2 3 4 6 5
bench_stdin "zsv (fast)"     "$SELECT" --no-trim -n --parser fast    -- 1 2 3 4 6 5
bench_stdin "zsv -q (default)" "$SELECT" --no-quote --no-trim -n --parser default -- 1 2 3 4 6 5
bench_stdin "zsv -q (fast)"    "$SELECT" --no-quote --no-trim -n --parser fast    -- 1 2 3 4 6 5
[ -n "$XSV" ] && bench_stdin "xsv" "$XSV" select 1,2,3,4,6,5
[ -n "$XAN" ] && bench_stdin "xan" "$XAN" select 1,2,3,4,6,5
