#!/usr/bin/env bash
#
# Benchmark: zsv fast parser quoting modes vs other CSV tools
#
# Usage: bench_quoting.sh <zsv_cli_binary> [output_file]
#
# The zsv binary should be built from the current repo. The script will
# test it in compat mode and fast mode.
# External tools (xsv, xan, polars, duckdb) are used if found in PATH.
#
# Works on Linux (x86_64) and macOS (arm64/NEON).

set -euo pipefail

ZSV="${1:?Usage: bench_quoting.sh <zsv_cli_binary> [output_file]}"
OUTFILE="${2:-}"

ITERS=5
ROWS=500000
COLS=20

# --- Platform detection ---
OS=$(uname -s)
ARCH=$(uname -m)
if [ "$OS" = "Darwin" ]; then
  CPU=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "Apple Silicon")
else
  CPU=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/.*: //' || echo "unknown")
fi

# --- Estimate disk I/O bandwidth ---
# Measures sequential read throughput by reading a real file from disk.
# The benchmark data files are used (after generation) so the estimate
# reflects the actual storage backing this benchmark.
estimate_disk_bw() {
  local file="$1"
  local bytes
  if [ "$OS" = "Darwin" ]; then
    bytes=$(stat -f%z "$file")
  else
    bytes=$(stat -c%s "$file")
  fi
  # Drop filesystem cache if possible (Linux only, requires root — skip if unavailable)
  sync 2>/dev/null
  if [ -w /proc/sys/vm/drop_caches ]; then
    echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
  fi
  TIMEFORMAT='%R'
  local best=999
  for _ in 1 2 3; do
    local t
    t=$( { time cat "$file" > /dev/null; } 2>&1 )
    if [ "$(echo "$t < $best" | bc)" = "1" ]; then best=$t; fi
  done
  if [ "$(echo "$best > 0" | bc)" = "1" ]; then
    echo "scale=1; $bytes / $best / 1073741824" | bc
  else
    echo "0"
  fi
}

# --- Temp directory (cleaned up on exit) ---
BENCH_TMPDIR=$(mktemp -d)
trap 'rm -rf "$BENCH_TMPDIR"' EXIT

# --- Portable key-value store (works with bash 3.2) ---
# Uses files under $BENCH_TMPDIR/_kv/ instead of associative arrays.
mkdir -p "$BENCH_TMPDIR/_kv"
kv_set() { printf '%s' "$2" > "$BENCH_TMPDIR/_kv/$1"; }
kv_get() { cat "$BENCH_TMPDIR/_kv/$1" 2>/dev/null || true; }

# --- Generate benchmark data ---
echo "Generating benchmark data ($ROWS rows x $COLS cols)..." >&2
python3 - "$BENCH_TMPDIR" "$ROWS" "$COLS" << 'PYGEN'
import random, os, sys
random.seed(42)
outdir, ROWS, COLS = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
header = ",".join(f"col{i}" for i in range(COLS))

for name, gen in [
    ("unquoted", lambda r, c: f"val{r}_{c}"),
    ("sparse_quoted", lambda r, c: '"hello, world"' if random.randint(0,49)==0 else f"val{r}_{c}"),
    ("standard_quoted", lambda r, c: ('"hello, world"' if (k:=random.randint(0,9))==0 else '"She said ""hi""!"' if k==1 else f"val{r}_{c}")),
    ("nonstandard_quoted", lambda r, c: ('"hello, world"' if (k:=random.randint(0,14))==0 else '12" monitor' if k==1 else '"She said ""hi""!"' if k==2 else 'say "hello" world' if k==3 else f"val{r}_{c}")),
]:
    path = os.path.join(outdir, f"{name}.csv")
    with open(path, "w") as f:
        f.write(header + "\n")
        for r in range(ROWS):
            f.write(",".join(gen(r, c) for c in range(COLS)) + "\n")
    sz = os.path.getsize(path)
    print(f"  {name}.csv: {sz/1024/1024:.0f} MB", file=sys.stderr)
PYGEN

# Estimate disk I/O bandwidth using the largest generated file
echo "Estimating disk I/O bandwidth..." >&2
HW_MAX_GBS=$(estimate_disk_bw "$BENCH_TMPDIR/nonstandard_quoted.csv")
echo "  Estimated: ${HW_MAX_GBS} GB/s (sequential read)" >&2

# Warm filesystem cache
cat "$BENCH_TMPDIR"/*.csv > /dev/null 2>&1

# Record file sizes in bytes
for DATA in unquoted sparse_quoted standard_quoted nonstandard_quoted; do
  if [ "$OS" = "Darwin" ]; then
    kv_set "filesize_${DATA}" "$(stat -f%z "$BENCH_TMPDIR/${DATA}.csv")"
  else
    kv_set "filesize_${DATA}" "$(stat -c%s "$BENCH_TMPDIR/${DATA}.csv")"
  fi
done

# --- Tool detection ---
detect() { command -v "$1" >/dev/null 2>&1; }

HAVE_XSV=0; detect xsv && HAVE_XSV=1
HAVE_XAN=0; detect xan && HAVE_XAN=1
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
POLARS_VENV="$SCRIPT_DIR/venv/bin/activate"
HAVE_POLARS=0
if [ -f "$POLARS_VENV" ]; then
  # polars is a Python library; activate the venv and check import
  if (. "$POLARS_VENV"; python3 -c 'import polars' 2>/dev/null); then
    HAVE_POLARS=1
  fi
fi
HAVE_QSV=0; QSV_BIN=""
for qbin in qsvlite qsv; do
  if detect "$qbin" && "$qbin" --version >/dev/null 2>&1; then
    HAVE_QSV=1; QSV_BIN="$qbin"; break
  fi
done
HAVE_DUCKDB=0; detect duckdb && HAVE_DUCKDB=1

# --- polars helper: run python3 with polars venv ---
run_polars() {
  . "$POLARS_VENV"
  POLARS_MAX_THREADS=1 python3 "$@"
}
export -f run_polars
export POLARS_VENV

# --- md5 tool (Linux: md5sum, macOS: md5 -r) ---
md5tool() {
  if detect md5sum; then
    md5sum | cut -d' ' -f1
  else
    md5 -r | cut -d' ' -f1
  fi
}

# --- Portable best-of-N timing ---
best_of() {
  local cmd="$1" best=999 t
  TIMEFORMAT='%R'
  for _ in $(seq "$ITERS"); do
    t=$( { time eval "$cmd" > /dev/null 2>&1; } 2>&1 )
    if [ "$(echo "$t < $best" | bc)" = "1" ]; then best=$t; fi
  done
  echo "$best"
}

# --- Correctness check ---
# Extracts the row count from tool output (first standalone number on its own line).
extract_count() {
  local output="$1"
  echo "$output" | grep -xE '[[:space:]]*[0-9]+[[:space:]]*' | tr -d '[:space:]' | head -1
}

check_correct() {
  local tool_cmd="$1" ref="$2"
  local output got errtmp
  errtmp="$BENCH_TMPDIR/check_stderr.tmp"
  output=$(eval "$tool_cmd" 2>"$errtmp")
  got=$(extract_count "$output")
  # Fail if count doesn't match OR if parser warnings were emitted on stderr
  if [ "$got" != "$ref" ]; then echo "incorrect"; return; fi
  if grep -qiE 'warning|error|truncat' "$errtmp" 2>/dev/null; then echo "incorrect"; return; fi
  echo "correct"
}

# --- Run benchmarks ---
echo "Running benchmarks ($ITERS iterations each, best-of-$ITERS)..." >&2

for DATA in unquoted sparse_quoted standard_quoted nonstandard_quoted; do
  FILE="$BENCH_TMPDIR/${DATA}.csv"
  REF=$("$ZSV" count --parser compat "$FILE")

  for CMD in count select; do
    KEY="${DATA}:${CMD}"

    # --no-trim: disable whitespace trimming for select (other tools don't trim)
    ZSV_SELECT_OPTS=""
    if [ "$CMD" = "select" ]; then ZSV_SELECT_OPTS="--no-trim"; fi

    # zsv compat
    kv_set "time_${KEY}:zsv-compat" "$(best_of "\"$ZSV\" $CMD $ZSV_SELECT_OPTS --parser compat \"$FILE\"")"
    kv_set "correct_${KEY}:zsv-compat" "correct"

    # zsv fast (prefix-XOR SIMD)
    kv_set "time_${KEY}:zsv-fast" "$(best_of "\"$ZSV\" $CMD $ZSV_SELECT_OPTS --parser fast \"$FILE\"")"
    if [ "$CMD" = "count" ]; then
      kv_set "correct_${KEY}:zsv-fast" "$(check_correct "\"$ZSV\" count --parser fast \"$FILE\"" "$REF")"
    else
      "$ZSV" $CMD $ZSV_SELECT_OPTS --parser compat "$FILE" | md5tool > "$BENCH_TMPDIR/ref.md5"
      "$ZSV" $CMD $ZSV_SELECT_OPTS --parser fast "$FILE" | md5tool > "$BENCH_TMPDIR/test.md5"
      if diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
        kv_set "correct_${KEY}:zsv-fast" "correct"
      else
        kv_set "correct_${KEY}:zsv-fast" "incorrect"
      fi
    fi

    # xsv
    if [ "$HAVE_XSV" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        kv_set "time_${KEY}:xsv" "$(best_of "xsv count \"$FILE\"")"
        kv_set "correct_${KEY}:xsv" "$(check_correct "xsv count \"$FILE\"" "$REF")"
      else
        kv_set "time_${KEY}:xsv" "$(best_of "xsv select 1- \"$FILE\"")"
        xsv select 1- "$FILE" | md5tool > "$BENCH_TMPDIR/test.md5"
        if diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
          kv_set "correct_${KEY}:xsv" "correct"
        else
          kv_set "correct_${KEY}:xsv" "incorrect"
        fi
      fi
    fi

    # xan
    if [ "$HAVE_XAN" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        kv_set "time_${KEY}:xan" "$(best_of "xan count \"$FILE\"")"
        kv_set "correct_${KEY}:xan" "$(check_correct "xan count \"$FILE\"" "$REF")"
      else
        LAST_COL="col$((COLS - 1))"
        kv_set "time_${KEY}:xan" "$(best_of "xan select \"col0:${LAST_COL}\" \"$FILE\"")"
        if xan select "col0:${LAST_COL}" "$FILE" 2>/dev/null | md5tool > "$BENCH_TMPDIR/test.md5" && \
           diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
          kv_set "correct_${KEY}:xan" "correct"
        else
          kv_set "correct_${KEY}:xan" "incorrect"
        fi
      fi
    fi

    # polars (Python library via venv, single-threaded)
    if [ "$HAVE_POLARS" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        kv_set "time_${KEY}:polars" "$(best_of "run_polars -c \"import polars as pl; print(pl.scan_csv('$FILE').select(pl.len()).collect().item())\"")"
        kv_set "correct_${KEY}:polars" "$(check_correct "run_polars -c \"import polars as pl; print(pl.scan_csv('$FILE').select(pl.len()).collect().item())\"" "$REF")"
      else
        kv_set "time_${KEY}:polars" "$(best_of "run_polars -c \"import polars as pl; pl.scan_csv('$FILE', infer_schema=False).sink_csv('/dev/null')\"")"
        if run_polars -c "import polars as pl; pl.scan_csv('$FILE', infer_schema=False).collect().write_csv('/dev/stdout')" 2>/dev/null | md5tool > "$BENCH_TMPDIR/test.md5" && \
           diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
          kv_set "correct_${KEY}:polars" "correct"
        else
          kv_set "correct_${KEY}:polars" "incorrect"
        fi
      fi
    fi

    # qsv (qsvlite)
    if [ "$HAVE_QSV" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        kv_set "time_${KEY}:qsv" "$(best_of "$QSV_BIN count \"$FILE\"")"
        kv_set "correct_${KEY}:qsv" "$(check_correct "$QSV_BIN count \"$FILE\"" "$REF")"
      else
        kv_set "time_${KEY}:qsv" "$(best_of "$QSV_BIN select 1- \"$FILE\"")"
        if "$QSV_BIN" select 1- "$FILE" 2>/dev/null | md5tool > "$BENCH_TMPDIR/test.md5" && \
           diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
          kv_set "correct_${KEY}:qsv" "correct"
        else
          kv_set "correct_${KEY}:qsv" "incorrect"
        fi
      fi
    fi

    # duckdb (default, multi-threaded)
    if [ "$HAVE_DUCKDB" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        kv_set "time_${KEY}:duckdb" "$(best_of "duckdb -noheader -csv -c \"SELECT count(*) FROM '$FILE'\"")"
        kv_set "correct_${KEY}:duckdb" "$(check_correct "duckdb -noheader -csv -c \"SELECT count(*) FROM '$FILE'\"" "$REF")"
      else
        kv_set "time_${KEY}:duckdb" "$(best_of "duckdb -noheader -csv -c \"COPY (FROM read_csv('$FILE', all_varchar=true)) TO '/dev/null'\"")"
        if duckdb -noheader -csv -c "COPY (FROM read_csv('$FILE', all_varchar=true)) TO '/dev/stdout'" 2>/dev/null | md5tool > "$BENCH_TMPDIR/test.md5" && \
           diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
          kv_set "correct_${KEY}:duckdb" "correct"
        else
          kv_set "correct_${KEY}:duckdb" "incorrect"
        fi
      fi

      # duckdb QUOTE='' (multi-threaded, no quote recognition)
      if [ "$CMD" = "count" ]; then
        kv_set "time_${KEY}:duckdb-noq" "$(best_of "duckdb -noheader -csv -c \"SELECT count(*) FROM read_csv('$FILE',all_varchar=true,quote='')\"")"
        kv_set "correct_${KEY}:duckdb-noq" "$(check_correct "duckdb -noheader -csv -c \"SELECT count(*) FROM read_csv('$FILE',all_varchar=true,quote='')\"" "$REF")"
      else
        kv_set "time_${KEY}:duckdb-noq" "$(best_of "duckdb -noheader -csv -c \"COPY (FROM read_csv('$FILE',all_varchar=true,quote='')) TO '/dev/null' (quote '')\"")"
        if duckdb -noheader -csv -c "COPY (FROM read_csv('$FILE',all_varchar=true,quote='')) TO '/dev/stdout' (quote '')" 2>/dev/null | md5tool > "$BENCH_TMPDIR/test.md5" && \
           diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
          kv_set "correct_${KEY}:duckdb-noq" "correct"
        else
          kv_set "correct_${KEY}:duckdb-noq" "incorrect"
        fi
      fi

      # duckdb QUOTE='', single-threaded
      if [ "$CMD" = "count" ]; then
        kv_set "time_${KEY}:duckdb-noq-1t" "$(best_of "duckdb -noheader -csv -c \"SET threads=1; SELECT count(*) FROM read_csv('$FILE',all_varchar=true,quote='')\"")"
        kv_set "correct_${KEY}:duckdb-noq-1t" "$(check_correct "duckdb -noheader -csv -c \"SET threads=1; SELECT count(*) FROM read_csv('$FILE',all_varchar=true,quote='')\"" "$REF")"
      else
        kv_set "time_${KEY}:duckdb-noq-1t" "$(best_of "duckdb -noheader -csv -c \"SET threads=1; COPY (FROM read_csv('$FILE',all_varchar=true,quote='')) TO '/dev/null' (quote '')\"")"
        if duckdb -noheader -csv -c "SET threads=1; COPY (FROM read_csv('$FILE',all_varchar=true,quote='')) TO '/dev/stdout' (quote '')" 2>/dev/null | md5tool > "$BENCH_TMPDIR/test.md5" && \
           diff "$BENCH_TMPDIR/ref.md5" "$BENCH_TMPDIR/test.md5" > /dev/null 2>&1; then
          kv_set "correct_${KEY}:duckdb-noq-1t" "correct"
        else
          kv_set "correct_${KEY}:duckdb-noq-1t" "incorrect"
        fi
      fi
    fi

    # zsv compat --parallel
    kv_set "time_${KEY}:zsv-compat-par" "$(best_of "\"$ZSV\" $CMD $ZSV_SELECT_OPTS --parser compat --parallel \"$FILE\"")"
    # Parallel correctness: use count (order-independent). For select, parallel output
    # may reorder rows across chunks, so we check count match only.
    kv_set "correct_${KEY}:zsv-compat-par" "$(check_correct "\"$ZSV\" count --parser compat --parallel \"$FILE\"" "$REF")"

    # zsv fast --parallel
    kv_set "time_${KEY}:zsv-fast-par" "$(best_of "\"$ZSV\" $CMD $ZSV_SELECT_OPTS --parser fast --parallel \"$FILE\"")"
    kv_set "correct_${KEY}:zsv-fast-par" "$(check_correct "\"$ZSV\" count --parser fast --parallel \"$FILE\"" "$REF")"

    echo "  $DATA $CMD done" >&2
  done
done

# --- Format helpers ---

# Format time, or N/A if incorrect, or - if missing
fmt_time() {
  local key="$1" tool="$2"
  local tkey="${key}:${tool}"
  local t; t=$(kv_get "time_${tkey}")
  local c; c=$(kv_get "correct_${tkey}")
  if [ -z "$t" ]; then echo "-"; return; fi
  if [ "$c" = "incorrect" ]; then echo "N/A"; return; fi
  printf "%ss" "$t"
}

# Format as "X.XX GB/s" or N/A/dash
fmt_gbs() {
  local key="$1" tool="$2" data="$3"
  local tkey="${key}:${tool}"
  local t; t=$(kv_get "time_${tkey}")
  local c; c=$(kv_get "correct_${tkey}")
  if [ -z "$t" ]; then echo "-"; return; fi
  if [ "$c" = "incorrect" ]; then echo "N/A"; return; fi
  local bytes; bytes=$(kv_get "filesize_${data}")
  if [ "$(echo "$t > 0" | bc)" = "1" ]; then
    printf "%s" "$(echo "scale=2; $bytes / $t / 1073741824" | bc)"
  else
    echo "-"
  fi
}

# --- Enumerate tools in order ---
TOOLS="zsv-compat zsv-compat-par zsv-fast zsv-fast-par"
[ "$HAVE_XSV" = "1" ] && TOOLS="$TOOLS xsv"
[ "$HAVE_XAN" = "1" ] && TOOLS="$TOOLS xan"
[ "$HAVE_POLARS" = "1" ] && TOOLS="$TOOLS polars"
[ "$HAVE_QSV" = "1" ] && TOOLS="$TOOLS qsv"
[ "$HAVE_DUCKDB" = "1" ] && TOOLS="$TOOLS duckdb duckdb-noq duckdb-noq-1t"

# Is this tool a multi-threaded run?
is_multithread() {
  case "$1" in
    *-par|*-parallel|*--parallel) return 0 ;;
    duckdb-noq-1t|duckdb-1-thread|polars-1-thread) return 1 ;;
    duckdb|duckdb-noq|polars) return 0 ;;
    *) return 1 ;;
  esac
}

# Highlight a cell value: green dot + bold
highlight() { printf '<img src="https://placehold.co/15x15/28a745/28a745.png" width="15" height="15"> **%s**' "$1"; }

# Second-best highlight: yellow dot + bold
highlight2() { printf '<img src="https://placehold.co/15x15/f0ad4e/f0ad4e.png" width="15" height="15"> **%s**' "$1"; }

tool_label() {
  case "$1" in
    zsv-compat)     echo "zsv compat" ;;
    zsv-compat-par) echo "zsv compat --parallel" ;;
    zsv-fast)       echo "zsv fast" ;;
    zsv-fast-par)   echo "zsv fast --parallel" ;;
    xsv)            echo "xsv" ;;
    xan)            echo "xan" ;;
    polars)         echo "polars" ;;
    qsv)            echo "qsv" ;;
    duckdb)         echo "duckdb" ;;
    duckdb-noq)     echo "duckdb (QUOTE='')" ;;
    duckdb-noq-1t)  echo "duckdb 1-thread (QUOTE='')" ;;
    *)              echo "$1" ;;
  esac
}

# --- Generate markdown report ---
report() {
  local XSV_VER XAN_VER POLARS_VER DUCKDB_VER
  XSV_VER=$(xsv --version 2>&1 || true)
  XAN_VER=$(xan --version 2>&1 | head -1 || true)
  POLARS_VER=$(run_polars -c "import polars as pl; print(pl.__version__)" 2>/dev/null || true)
  DUCKDB_VER=$(duckdb -version 2>&1 | head -1 || true)

  cat << HEADER
# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: $(date +%F)
Platform: $OS $ARCH
CPU: $CPU (single-threaded)
Estimated disk I/O bandwidth: ${HW_MAX_GBS} GB/s (sequential read)
Data: $ROWS rows x $COLS columns, best-of-$ITERS iterations

## Tools

| Tool | Version | Notes |
|------|---------|-------|
| zsv compat | (this repo) | Standard character-by-character parser |
| zsv compat --parallel | (this repo) | Compat parser, multi-threaded |
| zsv fast | (this repo) | SIMD parser, prefix-XOR for quoted blocks |
| zsv fast --parallel | (this repo) | SIMD parser, multi-threaded |
HEADER
  [ "$HAVE_XSV" = "1" ] && echo "| xsv | $XSV_VER | BurntSushi/xsv |"
  [ "$HAVE_XAN" = "1" ] && echo "| xan | $XAN_VER | medialab/xan |"
  [ "$HAVE_POLARS" = "1" ] && echo "| polars | $POLARS_VER | Python polars library (single-threaded, via venv) |"
  [ "$HAVE_QSV" = "1" ] && echo "| qsv | $($QSV_BIN --version 2>&1 | head -1 | cut -d' ' -f1-2) | jqnatividad/qsv (${QSV_BIN}) |"
  if [ "$HAVE_DUCKDB" = "1" ]; then
    echo "| duckdb | $DUCKDB_VER | Default (multi-threaded, with quote handling) |"
    echo "| duckdb (QUOTE='') | $DUCKDB_VER | Multi-threaded, no quote recognition |"
    echo "| duckdb 1-thread (QUOTE='') | $DUCKDB_VER | Single-threaded, no quote recognition |"
  fi

  cat << 'DATASETS'

## Datasets

| Name | Size | Description |
|------|------|-------------|
| unquoted | ~118 MB | No quote characters anywhere |
| sparse_quoted | ~118 MB | ~2% of cells are standard quoted (with commas) |
| standard_quoted | ~127 MB | ~20% of cells are RFC 4180 quoted |
| nonstandard_quoted | ~127 MB | ~27% of cells have mid-cell quotes in unquoted fields plus standard quoted fields |

## Correctness

N/A = produces incorrect results for this dataset.

DATASETS

  # Correctness table
  local hdr="| Dataset |" sep="|---------|"
  for tool in $TOOLS; do
    hdr="$hdr $(tool_label "$tool") |"
    sep="$sep------|"
  done
  echo "$hdr"
  echo "$sep"
  for DATA in unquoted sparse_quoted standard_quoted nonstandard_quoted; do
    local key="${DATA}:count"
    local row="| $DATA |"
    for tool in $TOOLS; do
      local c; c=$(kv_get "correct_${key}:${tool}")
      if [ -z "$c" ]; then c="n/a"; fi
      if [ "$c" = "incorrect" ]; then c="**incorrect**"; fi
      if [ "$tool" = "polars" ]; then c="${c}*"; fi
      row="$row $c |"
    done
    echo "$row"
  done

  if [ "$HAVE_POLARS" = "1" ]; then
    cat << 'NOTE'

\*polars handles mid-cell quotes in unquoted fields but fails on other
nonstandard patterns (e.g. malformed quoted fields like `"bb"bb"b`).

NOTE
  fi

  # Results tables
  for CMD in count select; do
    if [ "$CMD" = "count" ]; then
      echo "## Results: count (row counting)"
    else
      echo "## Results: select (full parse + CSV output)"
    fi
    echo ""

    # Time table
    echo "### Wall time"
    echo ""
    local hdr="| Dataset |" sep="|---------|"
    for tool in $TOOLS; do
      hdr="$hdr $(tool_label "$tool") |"
      sep="$sep------|"
    done
    echo "$hdr"
    echo "$sep"
    for DATA in unquoted sparse_quoted standard_quoted nonstandard_quoted; do
      local key="${DATA}:${CMD}"

      # Find overall min, best single-threaded, and 2nd-best single-threaded times
      local best_time="" best_st_time="" second_st_time=""
      for tool in $TOOLS; do
        local t; t=$(kv_get "time_${key}:${tool}")
        local c; c=$(kv_get "correct_${key}:${tool}")
        [ -z "$t" ] && continue
        [ "$c" = "incorrect" ] && continue
        if [ -z "$best_time" ] || [ "$(echo "$t < $best_time" | bc)" = "1" ]; then
          best_time="$t"
        fi
        if ! is_multithread "$tool"; then
          if [ -z "$best_st_time" ] || [ "$(echo "$t < $best_st_time" | bc)" = "1" ]; then
            second_st_time="$best_st_time"
            best_st_time="$t"
          elif [ -z "$second_st_time" ] || [ "$(echo "$t < $second_st_time" | bc)" = "1" ]; then
            if [ "$t" != "$best_st_time" ]; then
              second_st_time="$t"
            fi
          fi
        fi
      done
      # Determine if the best is multi-threaded (need to also highlight best single-threaded)
      local best_is_mt=0
      if [ -n "$best_time" ] && [ -n "$best_st_time" ] && [ "$best_time" != "$best_st_time" ]; then
        best_is_mt=1
      fi

      local row="| $DATA |"
      for tool in $TOOLS; do
        local val; val=$(fmt_time "$key" "$tool")
        local t; t=$(kv_get "time_${key}:${tool}")
        if [ -n "$best_time" ] && [ "$t" = "$best_time" ] && [ "$val" != "N/A" ] && [ "$val" != "-" ]; then
          row="$row $(highlight "$val") |"
        elif [ "$best_is_mt" = "1" ] && [ -n "$best_st_time" ] && [ "$t" = "$best_st_time" ] && [ "$val" != "N/A" ] && [ "$val" != "-" ]; then
          row="$row $(highlight "$val") |"
        elif [ -n "$second_st_time" ] && [ "$t" = "$second_st_time" ] && [ "$val" != "N/A" ] && [ "$val" != "-" ] && ! is_multithread "$tool"; then
          row="$row $(highlight2 "$val") |"
        else
          row="$row $val |"
        fi
      done
      echo "$row"
    done
    echo ""

    # GB/s table
    echo "### Throughput (GB/s)"
    echo ""
    echo "Disk I/O bandwidth: ${HW_MAX_GBS} GB/s (sequential read)"
    echo ""
    local hdr="| Dataset |" sep="|---------|"
    for tool in $TOOLS; do
      hdr="$hdr $(tool_label "$tool") |"
      sep="$sep------|"
    done
    echo "$hdr"
    echo "$sep"
    for DATA in unquoted sparse_quoted standard_quoted nonstandard_quoted; do
      local key="${DATA}:${CMD}"

      # Find overall max, best single-threaded, and 2nd-best single-threaded throughput
      local best_gbs="" best_st_gbs="" second_st_gbs=""
      for tool in $TOOLS; do
        local gbs; gbs=$(fmt_gbs "$key" "$tool" "$DATA")
        [ "$gbs" = "N/A" ] || [ "$gbs" = "-" ] && continue
        if [ -z "$best_gbs" ] || [ "$(echo "$gbs > $best_gbs" | bc)" = "1" ]; then
          best_gbs="$gbs"
        fi
        if ! is_multithread "$tool"; then
          if [ -z "$best_st_gbs" ] || [ "$(echo "$gbs > $best_st_gbs" | bc)" = "1" ]; then
            second_st_gbs="$best_st_gbs"
            best_st_gbs="$gbs"
          elif [ -z "$second_st_gbs" ] || [ "$(echo "$gbs > $second_st_gbs" | bc)" = "1" ]; then
            if [ "$gbs" != "$best_st_gbs" ]; then
              second_st_gbs="$gbs"
            fi
          fi
        fi
      done
      local best_is_mt=0
      if [ -n "$best_gbs" ] && [ -n "$best_st_gbs" ] && [ "$best_gbs" != "$best_st_gbs" ]; then
        best_is_mt=1
      fi

      local row="| $DATA |"
      for tool in $TOOLS; do
        local gbs; gbs=$(fmt_gbs "$key" "$tool" "$DATA")
        if [ "$gbs" = "N/A" ] || [ "$gbs" = "-" ]; then
          row="$row $gbs |"
        elif [ "$gbs" = "$best_gbs" ]; then
          row="$row $(highlight "${gbs} GB/s") |"
        elif [ "$best_is_mt" = "1" ] && [ "$gbs" = "$best_st_gbs" ]; then
          row="$row $(highlight "${gbs} GB/s") |"
        elif [ -n "$second_st_gbs" ] && [ "$gbs" = "$second_st_gbs" ] && ! is_multithread "$tool"; then
          row="$row $(highlight2 "${gbs} GB/s") |"
        else
          row="$row ${gbs} GB/s |"
        fi
      done
      echo "$row"
    done
    echo ""
  done
}

OUTPUT=$(report)

if [ -n "$OUTFILE" ]; then
  echo "$OUTPUT" > "$OUTFILE"
  echo "Results written to $OUTFILE" >&2
else
  echo "$OUTPUT"
fi
