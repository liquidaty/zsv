#!/bin/sh

set -e

echo "[INF] Running $0"

MIN_RUNS=5
MAX_RUNS=100

OS=${OS:-}
RUNS=${RUNS:-"$MIN_RUNS"}
BENCHMARKS_DIR=${BENCHMARKS_DIR:-".benchmarks"}
ZSV_LINUX_BUILD_COMPILER=${ZSV_LINUX_BUILD_COMPILER:-gcc}
ZSV_TAG=${ZSV_TAG:-}

if [ "$OS" = "" ]; then
  if [ "$CI" = true ]; then
    OS="$RUNNER_OS"
  else
    OS="$(uname -s)"
  fi
  OS=$(echo "$OS" | tr '[:upper:]' '[:lower:]')
fi

echo "[INF] OS: $OS"
echo "[INF] RUNS: $RUNS"
echo "[INF] BENCHMARKS_DIR: $BENCHMARKS_DIR"

if [ "$RUNS" -lt "$MIN_RUNS" ] || [ "$RUNS" -gt "$MAX_RUNS" ]; then
  echo "[WRN] Invalid RUNS value! [$RUNS]"
  echo "[WRN] RUNS must be >= $MIN_RUNS and <= $MAX_RUNS!"
  echo "[WRN] Using default minimum value... [RUNS: $MIN_RUNS]"
  RUNS="$MAX_RUNS"
fi

RUNS=$((RUNS + 1))

if [ "$ZSV_TAG" = "" ]; then
  ZSV_TAG=$(git ls-remote --tags --refs https://github.com/liquidaty/zsv | tail -n1 | cut -d '/' -f3)
fi

ZSV_TAG="$(echo "$ZSV_TAG" | sed 's/^v//')"
echo "[INF] ZSV_TAG: $ZSV_TAG"

ZSV_BUILD_FROM="[Release (v$ZSV_TAG)](https://github.com/liquidaty/zsv/releases/tag/v$ZSV_TAG)"
if [ "$CI" = true ] && [ "$WORKFLOW_RUN_ID" != "" ]; then
  ZSV_BUILD_FROM="[Workflow Run ($WORKFLOW_RUN_ID)](https://github.com/liquidaty/zsv/actions/runs/$WORKFLOW_RUN_ID)"
  echo "[INF] WORKFLOW_RUN_ID: $WORKFLOW_RUN_ID"
fi

OS_COMPILER=
if [ "$OS" = "linux" ]; then
  echo "[INF] ZSV_LINUX_BUILD_COMPILER: $ZSV_LINUX_BUILD_COMPILER"
  if [ "$ZSV_LINUX_BUILD_COMPILER" != "gcc" ] && [ "$ZSV_LINUX_BUILD_COMPILER" != "clang" ] && [ "$ZSV_LINUX_BUILD_COMPILER" != "musl" ]; then
    echo "[INF] Unknown value for ZSV_LINUX_BUILD_COMPILER! [$ZSV_LINUX_BUILD_COMPILER]"
    exit 1
  fi
  OS_COMPILER="$OS | $ZSV_LINUX_BUILD_COMPILER"
  ZSV_TAR_URL="https://github.com/liquidaty/zsv/releases/download/v$ZSV_TAG/zsv-$ZSV_TAG-amd64-linux-$ZSV_LINUX_BUILD_COMPILER.tar.gz"
  TSV_TAR_URL="https://github.com/eBay/tsv-utils/releases/download/v2.2.0/tsv-utils-v2.2.0_linux-x86_64_ldc2.tar.gz"
  XSV_TAR_URL="https://github.com/BurntSushi/xsv/releases/download/0.13.0/xsv-0.13.0-x86_64-unknown-linux-musl.tar.gz"
elif [ "$OS" = "macos" ] || [ "$OS" = "darwin" ]; then
  OS_COMPILER="$OS | gcc"
  ZSV_TAR_URL="https://github.com/liquidaty/zsv/releases/download/v$ZSV_TAG/zsv-$ZSV_TAG-amd64-macosx-gcc.tar.gz"
  TSV_TAR_URL="https://github.com/eBay/tsv-utils/releases/download/v2.2.1/tsv-utils-v2.2.1_osx-x86_64_ldc2.tar.gz"
  XSV_TAR_URL="https://github.com/BurntSushi/xsv/releases/download/0.13.0/xsv-0.13.0-x86_64-apple-darwin.tar.gz"
else
  echo "[ERR] OS not supported! [$OS]"
  exit 1
fi

mkdir -p "$BENCHMARKS_DIR"
cd "$BENCHMARKS_DIR"

find ./ -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +

CSV_URL="https://burntsushi.net/stuff/worldcitiespop_mil.csv"
CSV="$(echo "$CSV_URL" | sed 's:.*/::')"
printf "[INF] Downloading CSV file... [%s] " "$CSV"
if [ ! -f "$CSV" ]; then
  wget -q "$CSV_URL"
  echo "[DOWNLOADED]"
else
  echo "[SKIPPED]"
fi

for URL in "$ZSV_TAR_URL" "$TSV_TAR_URL" "$XSV_TAR_URL"; do
  TAR="$(echo "$URL" | sed 's:.*/::')"
  printf "[INF] Downloading... [%s] " "$TAR"
  if [ ! -f "$TAR" ]; then
    wget -q "$URL"
    echo "[DONE]"
  else
    echo "[SKIPPED]"
  fi
  printf "[INF] Extracting... [%s] " "$TAR"
  tar xf "$TAR"
  echo "[DONE]"
done

TOOLS_DIR="tools"
rm -rf ./"$TOOLS_DIR"
mkdir -p "$TOOLS_DIR"

FILES="$(find . -type f)"
for FILE in $FILES; do
  if [ -x "$FILE" ]; then
    mv "$FILE" "$TOOLS_DIR"
  fi
done

COUNT_OUTPUT_FILE="count.out"
SELECT_OUTPUT_FILE="select.out"
rm -f "$COUNT_OUTPUT_FILE" "$SELECT_OUTPUT_FILE"

echo "[INF] Running count benchmarks..."
for TOOL in zsv tsv xsv; do
  CMD=
  if [ "$TOOL" = "zsv" ]; then
    CMD="$TOOLS_DIR/zsv count"
  elif [ "$TOOL" = "xsv" ]; then
    CMD="$TOOLS_DIR/xsv count --no-headers"
  elif [ "$TOOL" = "tsv" ]; then
    CMD="$TOOLS_DIR/tsv-summarize --count -d,"
  fi

  I=0
  while [ "$I" -lt "$RUNS" ]; do
    if [ "$I" != 0 ]; then
      {
        printf "%d | %s : " "$I" "$TOOL"
        (time -p $CMD <"$CSV" >/dev/null) 2>&1 | xargs
      } | tee -a "$COUNT_OUTPUT_FILE"
    fi
    I=$((I + 1))
  done
done

echo "[INF] Running select benchmarks..."
for TOOL in zsv tsv xsv; do
  CMD=
  if [ "$TOOL" = "zsv" ]; then
    CMD="$TOOLS_DIR/zsv select -W -n -- 2 1 3-7"
  elif [ "$TOOL" = "xsv" ]; then
    CMD="$TOOLS_DIR/xsv select 2,1,3-7"
  elif [ "$TOOL" = "tsv" ]; then
    CMD="$TOOLS_DIR/tsv-select -d, -f 2,1,3-7"
  fi

  I=0
  while [ "$I" -lt "$RUNS" ]; do
    if [ "$I" != 0 ]; then
      {
        printf "%d | %s : " "$I" "$TOOL"
        (time -p $CMD <"$CSV" >/dev/null) 2>&1 | xargs
      } | tee -a "$SELECT_OUTPUT_FILE"
    fi
    I=$((I + 1))
  done
done

echo "[INF] Evaluating results..."

RUNS=$((RUNS - 1))

COUNT_MAX_REAL_TIME=$(cut -d ' ' -f6 <"$COUNT_OUTPUT_FILE" | sort | tail -n1)
COUNT_ZSV_REAL_TIME_VALUES=$(grep zsv "$COUNT_OUTPUT_FILE" | cut -d ' ' -f6 | xargs | tr ' ' '+')
COUNT_ZSV_AVG_REAL_TIME=$(printf %.2f "$(echo "($COUNT_ZSV_REAL_TIME_VALUES) / $RUNS" | bc -l)")
COUNT_TSV_REAL_TIME_VALUES=$(grep tsv "$COUNT_OUTPUT_FILE" | cut -d ' ' -f6 | xargs | tr ' ' '+')
COUNT_TSV_AVG_REAL_TIME=$(printf %.2f "$(echo "($COUNT_TSV_REAL_TIME_VALUES) / $RUNS" | bc -l)")
COUNT_XSV_REAL_TIME_VALUES=$(grep xsv "$COUNT_OUTPUT_FILE" | cut -d ' ' -f6 | xargs | tr ' ' '+')
COUNT_XSV_AVG_REAL_TIME=$(printf %.2f "$(echo "($COUNT_XSV_REAL_TIME_VALUES) / $RUNS" | bc -l)")

SELECT_MAX_REAL_TIME=$(cut -d ' ' -f6 <"$SELECT_OUTPUT_FILE" | sort | tail -n1)
SELECT_ZSV_REAL_TIME_VALUES=$(grep zsv "$SELECT_OUTPUT_FILE" | cut -d ' ' -f6 | xargs | tr ' ' '+')
SELECT_ZSV_AVG_REAL_TIME=$(printf %.2f "$(echo "($SELECT_ZSV_REAL_TIME_VALUES) / $RUNS" | bc -l)")
SELECT_TSV_REAL_TIME_VALUES=$(grep tsv "$SELECT_OUTPUT_FILE" | cut -d ' ' -f6 | xargs | tr ' ' '+')
SELECT_TSV_AVG_REAL_TIME=$(printf %.2f "$(echo "($SELECT_TSV_REAL_TIME_VALUES) / $RUNS" | bc -l)")
SELECT_XSV_REAL_TIME_VALUES=$(grep xsv "$SELECT_OUTPUT_FILE" | cut -d ' ' -f6 | xargs | tr ' ' '+')
SELECT_XSV_AVG_REAL_TIME=$(printf %.2f "$(echo "($SELECT_XSV_REAL_TIME_VALUES) / $RUNS" | bc -l)")

echo "[INF] COUNT_MAX_REAL_TIME: $COUNT_MAX_REAL_TIME"
echo "[INF] COUNT_ZSV_AVG_REAL_TIME: [($COUNT_ZSV_REAL_TIME_VALUES) / $RUNS = $COUNT_ZSV_AVG_REAL_TIME]"
echo "[INF] COUNT_TSV_AVG_REAL_TIME: [($COUNT_TSV_REAL_TIME_VALUES) / $RUNS = $COUNT_TSV_AVG_REAL_TIME]"
echo "[INF] COUNT_XSV_AVG_REAL_TIME: [($COUNT_XSV_REAL_TIME_VALUES) / $RUNS = $COUNT_XSV_AVG_REAL_TIME]"

echo "[INF] SELECT_MAX_REAL_TIME: $SELECT_MAX_REAL_TIME"
echo "[INF] SELECT_ZSV_AVG_REAL_TIME: [($SELECT_ZSV_REAL_TIME_VALUES) / $RUNS = $SELECT_ZSV_AVG_REAL_TIME]"
echo "[INF] SELECT_TSV_AVG_REAL_TIME: [($SELECT_TSV_REAL_TIME_VALUES) / $RUNS = $SELECT_TSV_AVG_REAL_TIME]"
echo "[INF] SELECT_XSV_AVG_REAL_TIME: [($SELECT_XSV_REAL_TIME_VALUES) / $RUNS = $SELECT_XSV_AVG_REAL_TIME]"

COUNT_MAX_REAL_TIME_FOR_CHART=$(printf %.2f "$(echo "$COUNT_MAX_REAL_TIME + ($COUNT_MAX_REAL_TIME * 0.1)" | bc -l)")
SELECT_MAX_REAL_TIME_FOR_CHART=$(printf %.2f "$(echo "$SELECT_MAX_REAL_TIME + ($SELECT_MAX_REAL_TIME * 0.1)" | bc -l)")

MARKDOWN_OUTPUT="benchmarks.md"
echo "[INF] Generating Markdown output... [$MARKDOWN_OUTPUT]"
{
  echo "## Benchmarks [$OS_COMPILER]"
  echo
  echo "- \`zsv\` build from: $ZSV_BUILD_FROM"
  echo "- Builds: [\`zsv\`]($ZSV_TAR_URL), [\`tsv\`]($TSV_TAR_URL), [\`xsv\`]($XSV_TAR_URL)"
  echo
  echo "### \`count\`"
  echo
  echo "<details>"
  echo "<summary>Runs (click to expand/collapse)</summary>"
  echo
  echo '```text'
  cat "$COUNT_OUTPUT_FILE"
  echo
  echo "Averages:"
  echo "- zsv: [($COUNT_ZSV_REAL_TIME_VALUES) / $RUNS = $COUNT_ZSV_AVG_REAL_TIME]"
  echo "- tsv: [($COUNT_TSV_REAL_TIME_VALUES) / $RUNS = $COUNT_TSV_AVG_REAL_TIME]"
  echo "- xsv: [($COUNT_XSV_REAL_TIME_VALUES) / $RUNS = $COUNT_XSV_AVG_REAL_TIME]"
  echo '```'
  echo
  echo "</details>"
  echo
  echo '```mermaid'
  echo '---'
  echo 'config:'
  echo '  xyChart:'
  echo '    width: 600'
  echo '    height: 400'
  echo '  themeVariables:'
  echo '    xyChart:'
  echo '      titleColor: "#1e90ff"'
  echo '      plotColorPalette: "#1e90ff"'
  echo '---'
  echo "xychart-beta"
  echo "  title \"count speed [lower = faster]\""
  echo "  x-axis [zsv, tsv, xsv]"
  echo "  y-axis \"time\" 0 --> $COUNT_MAX_REAL_TIME_FOR_CHART"
  echo "  bar [$COUNT_ZSV_AVG_REAL_TIME, $COUNT_TSV_AVG_REAL_TIME, $COUNT_XSV_AVG_REAL_TIME]"
  echo '```'
  echo
  echo "### \`select\`"
  echo
  echo "<details>"
  echo "<summary>Runs (click to expand/collapse)</summary>"
  echo
  echo '```text'
  cat "$SELECT_OUTPUT_FILE"
  echo
  echo "Averages:"
  echo "- zsv: [($SELECT_ZSV_REAL_TIME_VALUES) / $RUNS = $SELECT_ZSV_AVG_REAL_TIME]"
  echo "- tsv: [($SELECT_TSV_REAL_TIME_VALUES) / $RUNS = $SELECT_TSV_AVG_REAL_TIME]"
  echo "- xsv: [($SELECT_XSV_REAL_TIME_VALUES) / $RUNS = $SELECT_XSV_AVG_REAL_TIME]"
  echo '```'
  echo
  echo "</details>"
  echo
  echo '```mermaid'
  echo '---'
  echo 'config:'
  echo '  xyChart:'
  echo '    width: 600'
  echo '    height: 400'
  echo '  themeVariables:'
  echo '    xyChart:'
  echo '      titleColor: "#1e90ff"'
  echo '      plotColorPalette: "#1e90ff"'
  echo '---'
  echo "xychart-beta"
  echo "  title \"select speed [lower = faster]\""
  echo "  x-axis [zsv, tsv, xsv]"
  echo "  y-axis \"time\" 0 --> $SELECT_MAX_REAL_TIME_FOR_CHART"
  echo "  bar [$SELECT_ZSV_AVG_REAL_TIME, $SELECT_TSV_AVG_REAL_TIME, $SELECT_XSV_AVG_REAL_TIME]"
  echo '```'
} >"$MARKDOWN_OUTPUT"
echo "[INF] Generated Markdown output successfully!"

if [ "$CI" = true ]; then
  echo "[INF] Generating step summary..."
  {
    cat "$MARKDOWN_OUTPUT"
  } >>"$GITHUB_STEP_SUMMARY"
  echo "[INF] Generated step summary successfully!"
fi

cd ..

echo "[INF] --- [DONE] ---"
