#!/usr/bin/env bash
#
# Benchmark: zsv fast parser quoting modes vs other CSV tools
#
# Usage: bench_quoting.sh <zsv_cli_binary> [output_file]
#
# The zsv binary should be built from the current repo. The script will
# test it in legacy mode, fast mode, and fast+--malformed-quoting mode.
# External tools (xsv, xan, polars) are used if found in PATH.
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

# --- Estimate single-core memory bandwidth ---
# Uses a simple dd read test as a practical upper bound for sequential read throughput.
estimate_membw() {
  # Read 256MB from /dev/zero to measure memory/bus throughput.
  # This overstates achievable bandwidth from storage but gives a ceiling.
  local bytes=268435456  # 256 MB
  TIMEFORMAT='%R'
  local best=999
  for _ in 1 2 3; do
    # Ensure we measure memory bandwidth, not storage
    local t
    t=$( { time dd if=/dev/zero of=/dev/null bs=1M count=256 2>/dev/null; } 2>&1 )
    if [ "$(echo "$t < $best" | bc)" = "1" ]; then best=$t; fi
  done
  if [ "$(echo "$best > 0" | bc)" = "1" ]; then
    echo "scale=1; $bytes / $best / 1073741824" | bc
  else
    echo "0"
  fi
}

echo "Estimating memory bandwidth..." >&2
HW_MAX_GBS=$(estimate_membw)
echo "  Estimated: ${HW_MAX_GBS} GB/s (single-core sequential read)" >&2

# --- Temp directory (cleaned up on exit) ---
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

# --- Generate benchmark data ---
echo "Generating benchmark data ($ROWS rows x $COLS cols)..." >&2
python3 - "$TMPDIR" "$ROWS" "$COLS" << 'PYGEN'
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

# Warm filesystem cache
cat "$TMPDIR"/*.csv > /dev/null 2>&1

# Record file sizes in bytes
declare -A FILESIZES
for DATA in unquoted sparse_quoted standard_quoted nonstandard_quoted; do
  if [ "$OS" = "Darwin" ]; then
    FILESIZES[$DATA]=$(stat -f%z "$TMPDIR/${DATA}.csv")
  else
    FILESIZES[$DATA]=$(stat -c%s "$TMPDIR/${DATA}.csv")
  fi
done

# --- Tool detection ---
detect() { command -v "$1" >/dev/null 2>&1; }

HAVE_XSV=0; detect xsv && HAVE_XSV=1
HAVE_XAN=0; detect xan && HAVE_XAN=1
HAVE_POLARS=0; detect polars && HAVE_POLARS=1
HAVE_QSV=0; QSV_BIN=""
for qbin in qsvlite qsv; do
  if detect "$qbin" && "$qbin" --version >/dev/null 2>&1; then
    HAVE_QSV=1; QSV_BIN="$qbin"; break
  fi
done

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
# Extracts the row count from tool output. Handles plain numbers (xsv/xan/zsv)
# and table-formatted output (polars: extracts from │ N │ lines).
extract_count() {
  local output="$1"
  # Try polars table format first: │ 500000 │
  local polars_match
  polars_match=$(echo "$output" | grep -E '^\│ [0-9]' | tr -d '│ ' | head -1)
  if [ -n "$polars_match" ]; then echo "$polars_match"; return; fi
  # Fall back to first standalone number on its own line
  echo "$output" | grep -xE '[[:space:]]*[0-9]+[[:space:]]*' | tr -d '[:space:]' | head -1
}

check_correct() {
  local tool_cmd="$1" ref="$2"
  local output got
  output=$(eval "$tool_cmd" 2>/dev/null)
  got=$(extract_count "$output")
  if [ "$got" = "$ref" ]; then echo "correct"; else echo "incorrect"; fi
}

# --- Run benchmarks ---
echo "Running benchmarks ($ITERS iterations each, best-of-$ITERS)..." >&2

declare -A TIMES CORRECT

for DATA in unquoted sparse_quoted standard_quoted nonstandard_quoted; do
  FILE="$TMPDIR/${DATA}.csv"
  REF=$("$ZSV" count --parser legacy "$FILE")

  for CMD in count select; do
    KEY="${DATA}:${CMD}"

    # zsv legacy
    TIMES["${KEY}:zsv-legacy"]=$(best_of "\"$ZSV\" $CMD --parser legacy \"$FILE\"")
    CORRECT["${KEY}:zsv-legacy"]="correct"

    # zsv fast (prefix-XOR, explicitly disable malformed quoting)
    TIMES["${KEY}:zsv-fast"]=$(best_of "\"$ZSV\" $CMD --parser fast --no-malformed-quoting \"$FILE\"")
    if [ "$CMD" = "count" ]; then
      CORRECT["${KEY}:zsv-fast"]=$(check_correct "\"$ZSV\" count --parser fast --no-malformed-quoting \"$FILE\"" "$REF")
    else
      "$ZSV" $CMD --parser legacy "$FILE" | md5tool > "$TMPDIR/ref.md5"
      "$ZSV" $CMD --parser fast --no-malformed-quoting "$FILE" | md5tool > "$TMPDIR/test.md5"
      if diff "$TMPDIR/ref.md5" "$TMPDIR/test.md5" > /dev/null 2>&1; then
        CORRECT["${KEY}:zsv-fast"]="correct"
      else
        CORRECT["${KEY}:zsv-fast"]="incorrect"
      fi
    fi

    # zsv fast+MQ (malformed quoting)
    TIMES["${KEY}:zsv-fast-mq"]=$(best_of "\"$ZSV\" $CMD --parser fast --malformed-quoting \"$FILE\"")
    if [ "$CMD" = "count" ]; then
      CORRECT["${KEY}:zsv-fast-mq"]=$(check_correct "\"$ZSV\" count --parser fast --malformed-quoting \"$FILE\"" "$REF")
    else
      "$ZSV" $CMD --parser fast --malformed-quoting "$FILE" | md5tool > "$TMPDIR/test.md5"
      if diff "$TMPDIR/ref.md5" "$TMPDIR/test.md5" > /dev/null 2>&1; then
        CORRECT["${KEY}:zsv-fast-mq"]="correct"
      else
        CORRECT["${KEY}:zsv-fast-mq"]="incorrect"
      fi
    fi

    # xsv
    if [ "$HAVE_XSV" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        TIMES["${KEY}:xsv"]=$(best_of "xsv count \"$FILE\"")
        CORRECT["${KEY}:xsv"]=$(check_correct "xsv count \"$FILE\"" "$REF")
      else
        TIMES["${KEY}:xsv"]=$(best_of "xsv select 1- \"$FILE\"")
        xsv select 1- "$FILE" | md5tool > "$TMPDIR/test.md5"
        if diff "$TMPDIR/ref.md5" "$TMPDIR/test.md5" > /dev/null 2>&1; then
          CORRECT["${KEY}:xsv"]="correct"
        else
          CORRECT["${KEY}:xsv"]="incorrect"
        fi
      fi
    fi

    # xan
    if [ "$HAVE_XAN" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        TIMES["${KEY}:xan"]=$(best_of "xan count \"$FILE\"")
        CORRECT["${KEY}:xan"]=$(check_correct "xan count \"$FILE\"" "$REF")
      else
        LAST_COL="col$((COLS - 1))"
        TIMES["${KEY}:xan"]=$(best_of "xan select \"col0:${LAST_COL}\" \"$FILE\"")
        if xan select "col0:${LAST_COL}" "$FILE" 2>/dev/null | md5tool > "$TMPDIR/test.md5" && \
           diff "$TMPDIR/ref.md5" "$TMPDIR/test.md5" > /dev/null 2>&1; then
          CORRECT["${KEY}:xan"]="correct"
        else
          CORRECT["${KEY}:xan"]="incorrect"
        fi
      fi
    fi

    # polars
    if [ "$HAVE_POLARS" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        TIMES["${KEY}:polars"]=$(best_of "polars -c \"SELECT COUNT(*) FROM read_csv('$FILE')\"")
        CORRECT["${KEY}:polars"]=$(check_correct "polars -c \"SELECT COUNT(*) FROM read_csv('$FILE')\"" "$REF")
      else
        TIMES["${KEY}:polars"]=$(best_of "polars -o csv -c \"SELECT * FROM read_csv('$FILE')\"")
        if polars -o csv -c "SELECT * FROM read_csv('$FILE')" 2>/dev/null | md5tool > "$TMPDIR/test.md5" && \
           diff "$TMPDIR/ref.md5" "$TMPDIR/test.md5" > /dev/null 2>&1; then
          CORRECT["${KEY}:polars"]="correct"
        else
          CORRECT["${KEY}:polars"]="incorrect"
        fi
      fi
    fi

    # qsv (qsvlite)
    if [ "$HAVE_QSV" = "1" ]; then
      if [ "$CMD" = "count" ]; then
        TIMES["${KEY}:qsv"]=$(best_of "$QSV_BIN count \"$FILE\"")
        CORRECT["${KEY}:qsv"]=$(check_correct "$QSV_BIN count \"$FILE\"" "$REF")
      else
        TIMES["${KEY}:qsv"]=$(best_of "$QSV_BIN select 1- \"$FILE\"")
        if "$QSV_BIN" select 1- "$FILE" 2>/dev/null | md5tool > "$TMPDIR/test.md5" && \
           diff "$TMPDIR/ref.md5" "$TMPDIR/test.md5" > /dev/null 2>&1; then
          CORRECT["${KEY}:qsv"]="correct"
        else
          CORRECT["${KEY}:qsv"]="incorrect"
        fi
      fi
    fi

    # zsv legacy --parallel
    TIMES["${KEY}:zsv-legacy-par"]=$(best_of "\"$ZSV\" $CMD --parser legacy --parallel \"$FILE\"")
    # Parallel correctness: use count (order-independent). For select, parallel output
    # may reorder rows across chunks, so we check count match only.
    CORRECT["${KEY}:zsv-legacy-par"]=$(check_correct "\"$ZSV\" count --parser legacy --parallel \"$FILE\"" "$REF")

    # zsv fast --parallel
    TIMES["${KEY}:zsv-fast-par"]=$(best_of "\"$ZSV\" $CMD --parser fast --no-malformed-quoting --parallel \"$FILE\"")
    CORRECT["${KEY}:zsv-fast-par"]=$(check_correct "\"$ZSV\" count --parser fast --no-malformed-quoting --parallel \"$FILE\"" "$REF")

    echo "  $DATA $CMD done" >&2
  done
done

# --- Format helpers ---

# Format time, or N/A if incorrect, or - if missing
fmt_time() {
  local key="$1" tool="$2"
  local tkey="${key}:${tool}"
  local t="${TIMES[$tkey]:-}" c="${CORRECT[$tkey]:-}"
  if [ -z "$t" ]; then echo "-"; return; fi
  if [ "$c" = "incorrect" ]; then echo "N/A"; return; fi
  printf "%ss" "$t"
}

# Format as "X.XX GB/s" or N/A/dash
fmt_gbs() {
  local key="$1" tool="$2" data="$3"
  local tkey="${key}:${tool}"
  local t="${TIMES[$tkey]:-}" c="${CORRECT[$tkey]:-}"
  if [ -z "$t" ]; then echo "-"; return; fi
  if [ "$c" = "incorrect" ]; then echo "N/A"; return; fi
  local bytes="${FILESIZES[$data]}"
  if [ "$(echo "$t > 0" | bc)" = "1" ]; then
    printf "%s" "$(echo "scale=2; $bytes / $t / 1073741824" | bc)"
  else
    echo "-"
  fi
}

# --- Enumerate tools in order ---
TOOLS="zsv-legacy zsv-legacy-par zsv-fast zsv-fast-par zsv-fast-mq"
[ "$HAVE_XSV" = "1" ] && TOOLS="$TOOLS xsv"
[ "$HAVE_XAN" = "1" ] && TOOLS="$TOOLS xan"
[ "$HAVE_POLARS" = "1" ] && TOOLS="$TOOLS polars"
[ "$HAVE_QSV" = "1" ] && TOOLS="$TOOLS qsv"

tool_label() {
  case "$1" in
    zsv-legacy)     echo "zsv legacy" ;;
    zsv-legacy-par) echo "zsv legacy --parallel" ;;
    zsv-fast)       echo "zsv fast" ;;
    zsv-fast-par)   echo "zsv fast --parallel" ;;
    zsv-fast-mq)    echo "zsv fast+MQ" ;;
    xsv)            echo "xsv" ;;
    xan)            echo "xan" ;;
    polars)         echo "polars" ;;
    qsv)            echo "qsv" ;;
    *)              echo "$1" ;;
  esac
}

# --- Generate markdown report ---
report() {
  local XSV_VER XAN_VER POLARS_VER
  XSV_VER=$(xsv --version 2>&1 || true)
  XAN_VER=$(xan --version 2>&1 | head -1 || true)
  POLARS_VER=$(polars --version 2>&1 | head -1 || true)

  cat << HEADER
# CSV Parser Benchmark: Fast Parser Quoting Modes

Date: $(date +%F)
Platform: $OS $ARCH
CPU: $CPU (single-threaded)
Estimated sequential read bandwidth: ${HW_MAX_GBS} GB/s (single-core, from \`dd if=/dev/zero\`)
Data: $ROWS rows x $COLS columns, best-of-$ITERS iterations

## Tools

| Tool | Version | Notes |
|------|---------|-------|
| zsv legacy | (this repo) | Standard character-by-character parser |
| zsv legacy --parallel | (this repo) | Legacy parser, multi-threaded |
| zsv fast | (this repo) | SIMD parser, prefix-XOR for quoted blocks |
| zsv fast --parallel | (this repo) | SIMD parser, multi-threaded |
| zsv fast+MQ | (this repo) | SIMD parser with \`--malformed-quoting\` (scalar for quoted blocks) |
HEADER
  [ "$HAVE_XSV" = "1" ] && echo "| xsv | $XSV_VER | BurntSushi/xsv |"
  [ "$HAVE_XAN" = "1" ] && echo "| xan | $XAN_VER | medialab/xan |"
  [ "$HAVE_POLARS" = "1" ] && echo "| polars | $POLARS_VER | polars-cli (SQL interface over Polars engine) |"
  [ "$HAVE_QSV" = "1" ] && echo "| qsv | $($QSV_BIN --version 2>&1 | head -1 | cut -d' ' -f1-2) | jqnatividad/qsv (${QSV_BIN}) |"

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
      local c="${CORRECT[${key}:${tool}]:-n/a}"
      if [ "$c" = "incorrect" ]; then c="**incorrect**"; fi
      if [ "$tool" = "polars" ]; then c="${c}*"; fi
      row="$row $c |"
    done
    echo "$row"
  done

  cat << 'NOTE'

\*polars handles mid-cell quotes in unquoted fields but fails on other
nonstandard patterns (e.g. malformed quoted fields like `"bb"bb"b`).

NOTE

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
      local row="| $DATA |"
      for tool in $TOOLS; do
        row="$row $(fmt_time "$key" "$tool") |"
      done
      echo "$row"
    done
    echo ""

    # GB/s table
    echo "### Throughput (GB/s)"
    echo ""
    echo "Hardware sequential read bandwidth: ${HW_MAX_GBS} GB/s"
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
      local row="| $DATA |"
      for tool in $TOOLS; do
        local gbs=$(fmt_gbs "$key" "$tool" "$DATA")
        if [ "$gbs" = "N/A" ] || [ "$gbs" = "-" ]; then
          row="$row $gbs |"
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
