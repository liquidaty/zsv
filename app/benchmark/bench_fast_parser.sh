#!/bin/sh
# Benchmark: compare zsv (default/fast), xsv, xan, and qsv
#
# Usage: sh app/benchmark/bench_fast_parser.sh [iterations]
#
# Requires: app/benchmark/worldcitiespop.csv
# Optional: app/benchmark/quoted_standard.csv, quoted_nonstandard.csv
#   (generate with: python3 app/benchmark/gen_quoted_test.py)

ITERATIONS=${1:-5}
CSV=app/benchmark/worldcitiespop.csv
QCSV=app/benchmark/quoted_standard.csv
NCSV=app/benchmark/quoted_nonstandard.csv
COUNT=./build/Darwin/rel/gcc-14/bin/zsv_count
SELECT=./build/Darwin/rel/gcc-14/bin/zsv_select
XSV=$(which xsv 2>/dev/null)
XAN=$(which xan 2>/dev/null)
QSV=$(which qsv 2>/dev/null)

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
      for(i=1;i<=NF;i++) if($i=="real") { print $(i-1); exit }
    }')
    if [ -z "$t" ]; then
      t=$( { /usr/bin/time "$@" > /dev/null; } 2>&1 | sed -n 's/.*real[[:space:]]*0m\([0-9.]*\)s.*/\1/p' )
    fi
    total=$(echo "$total + $t" | bc)
  done
  avg=$(echo "scale=4; $total / $ITERATIONS" | bc)
  printf "  %-40s avg %.4fs (%d runs)\n" "$label" "$avg" "$ITERATIONS"
}

bench_stdin() {
  label="$1"
  input="$2"
  shift 2
  total=0
  for i in $(seq 1 "$ITERATIONS"); do
    t=$( { /usr/bin/time "$@" < "$input" > /dev/null; } 2>&1 | awk '{
      for(i=1;i<=NF;i++) if($i=="real") { print $(i-1); exit }
    }')
    if [ -z "$t" ]; then
      t=$( { /usr/bin/time "$@" < "$input" > /dev/null; } 2>&1 | sed -n 's/.*real[[:space:]]*0m\([0-9.]*\)s.*/\1/p' )
    fi
    total=$(echo "$total + $t" | bc)
  done
  avg=$(echo "scale=4; $total / $ITERATIONS" | bc)
  printf "  %-40s avg %.4fs (%d runs)\n" "$label" "$avg" "$ITERATIONS"
}

run_test() {
  testcsv="$1"
  testname="$2"

  echo "=== $testname: count ==="
  bench "zsv (default)"  "$COUNT" --parser default "$testcsv"
  bench "zsv (fast)"     "$COUNT" --parser fast    "$testcsv"
  [ -n "$XSV" ] && bench "xsv" "$XSV" count "$testcsv"
  [ -n "$XAN" ] && bench "xan" "$XAN" count "$testcsv"
  [ -n "$QSV" ] && bench "qsv" "$QSV" count "$testcsv"

  echo ""
  echo "=== $testname: select 6 columns ==="
  bench_stdin "zsv (default)" "$testcsv" "$SELECT" --no-trim -n --parser default -- 1 2 3 4 6 5
  bench_stdin "zsv (fast)"    "$testcsv" "$SELECT" --no-trim -n --parser fast    -- 1 2 3 4 6 5
  [ -n "$XSV" ] && bench_stdin "xsv" "$testcsv" "$XSV" select 1,2,3,4,6,5
  [ -n "$XAN" ] && bench_stdin "xan" "$testcsv" "$XAN" select 1,2,3,4,6,5
  [ -n "$QSV" ] && bench_stdin "qsv" "$testcsv" "$QSV" select 1,2,3,4,6,5

  echo ""
}

SEL50="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50"
SEL50_ZSV="1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50"
SEL100_XSV="1-100"
SEL100_XAN="0:99"
SEL100_QSV="1-100"
SEL100_ZSV="1-100"

run_test "$CSV" "worldcitiespop (3.2M rows, 7 cols, mostly unquoted)"

if [ -f "$QCSV" ]; then
  run_test "$QCSV" "quoted_standard (500K rows, 100 cols, 15 heavily quoted)"

  echo "=== quoted_standard: select 50 columns ==="
  bench_stdin "zsv (default)" "$QCSV" "$SELECT" --no-trim -n --parser default -- $SEL50_ZSV
  bench_stdin "zsv (fast)"    "$QCSV" "$SELECT" --no-trim -n --parser fast    -- $SEL50_ZSV
  [ -n "$XSV" ] && bench_stdin "xsv" "$QCSV" "$XSV" select "$SEL50"
  [ -n "$XAN" ] && bench_stdin "xan" "$QCSV" "$XAN" select "$SEL50"
  [ -n "$QSV" ] && bench_stdin "qsv" "$QCSV" "$QSV" select "$SEL50"

  echo ""
  echo "=== quoted_standard: select 100 columns (all) ==="
  bench_stdin "zsv (default)" "$QCSV" "$SELECT" --no-trim -n --parser default -- $SEL100_ZSV
  bench_stdin "zsv (fast)"    "$QCSV" "$SELECT" --no-trim -n --parser fast    -- $SEL100_ZSV
  [ -n "$XSV" ] && bench_stdin "xsv" "$QCSV" "$XSV" select "$SEL100_XSV"
  [ -n "$XAN" ] && bench_stdin "xan" "$QCSV" "$XAN" select "$SEL100_XAN"
  [ -n "$QSV" ] && bench_stdin "qsv" "$QCSV" "$QSV" select "$SEL100_QSV"
  echo ""
fi

if [ -f "$NCSV" ]; then
  run_test "$NCSV" "quoted_nonstandard (500K rows, 100 cols, non-standard quoting)"
fi
