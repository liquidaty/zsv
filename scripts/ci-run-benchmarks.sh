#!/bin/sh

set -e

echo "[INF] Running $0"

OS=${OS:-}
RUNS=${RUNS:-6}
SKIP_FIRST_RUN=${SKIP_FIRST_RUN:-true}
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
echo "[INF] SKIP_FIRST_RUN: $SKIP_FIRST_RUN"
echo "[INF] BENCHMARKS_DIR: $BENCHMARKS_DIR"

if [ "$RUNS" -lt 2 ]; then
  echo "[ERR] RUNS must be greater than 2!"
  exit 1
fi

if [ "$ZSV_TAG" = "" ]; then
  ZSV_TAG=$(git ls-remote --tags --refs https://github.com/liquidaty/zsv | tail -n1 | cut -d '/' -f3)
fi

ZSV_TAG="$(echo "$ZSV_TAG" | sed 's/^v//')"
echo "[INF] ZSV_TAG: $ZSV_TAG"

if [ "$CI" = true ] && [ "$WORKFLOW_RUN_ID" != "" ]; then
  echo "[INF] WORKFLOW_RUN_ID: $WORKFLOW_RUN_ID"
fi

if [ "$OS" = "linux" ]; then
  echo "[INF] ZSV_LINUX_BUILD_COMPILER: $ZSV_LINUX_BUILD_COMPILER"
  if [ "$ZSV_LINUX_BUILD_COMPILER" != "gcc" ] && [ "$ZSV_LINUX_BUILD_COMPILER" != "clang" ] && [ "$ZSV_LINUX_BUILD_COMPILER" != "musl" ]; then
    echo "[INF] Unknown value for ZSV_LINUX_BUILD_COMPILER! [$ZSV_LINUX_BUILD_COMPILER]"
    exit 1
  fi
  ZSV_TAR_URL="https://github.com/liquidaty/zsv/releases/download/v$ZSV_TAG/zsv-$ZSV_TAG-amd64-linux-$ZSV_LINUX_BUILD_COMPILER.tar.gz"
  TSV_TAR_URL="https://github.com/eBay/tsv-utils/releases/download/v2.2.0/tsv-utils-v2.2.0_linux-x86_64_ldc2.tar.gz"
  XSV_TAR_URL="https://github.com/BurntSushi/xsv/releases/download/0.13.0/xsv-0.13.0-x86_64-unknown-linux-musl.tar.gz"
elif [ "$OS" = "macos" ] || [ "$OS" = "darwin" ]; then
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
    wget "$URL"
  echo "[DONE]"
else
  echo "[SKIPPED]"
  fi
  printf "[INF] Extracting... [%s] " "$TAR"
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
for TOOL in zsv xsv tsv; do
  CMD=
  if [ "$TOOL" = "zsv" ]; then
    CMD="$TOOLS_DIR/zsv count"
  elif [ "$TOOL" = "xsv" ]; then
    CMD="$TOOLS_DIR/xsv count"
  elif [ "$TOOL" = "tsv" ]; then
    CMD="$TOOLS_DIR/number-lines -d,"
  fi

  I=0
  while [ "$I" -lt "$RUNS" ]; do
    if [ "$SKIP_FIRST_RUN" = true ] && [ "$I" = 0 ]; then
      I=$((I + 1))
      continue
    fi
    {
      printf "%d | %s : " "$I" "$TOOL"
      (time -p $CMD <"$CSV" >/dev/null) 2>&1 | xargs
    } | tee -a "$COUNT_OUTPUT_FILE"
    I=$((I + 1))
  done
done

echo "[INF] Running select benchmarks..."
for TOOL in zsv xsv tsv; do
  CMD=
  if [ "$TOOL" = "zsv" ]; then
    CMD="$TOOLS_DIR/zsv select -W -n -- 2 1 3-7"
  elif [ "$TOOL" = "xsv" ]; then
    CMD="$TOOLS_DIR/xsv select 2,1,3-7"
  elif [ "$TOOL" = "tsv" ]; then
    CMD="$TOOLS_DIR/tsv-select -d, -f 1-7"
  fi

  I=0
  while [ "$I" -lt "$RUNS" ]; do
    if [ "$SKIP_FIRST_RUN" = true ] && [ "$I" = 0 ]; then
      I=$((I + 1))
      continue
    fi
    {
      printf "%d | %s : " "$I" "$TOOL"
      (time -p $CMD <"$CSV" >/dev/null) 2>&1 | xargs
    } | tee -a "$SELECT_OUTPUT_FILE"
    I=$((I + 1))
  done
done

MARKDOWN_OUTPUT="benchmarks.md"
echo "[INF] Generating Markdown output... [$MARKDOWN_OUTPUT]"
TIMESTAMP="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
{
  echo '# Benchmarks'
  echo
  echo "- Timestamp UTC: \`$TIMESTAMP\`"
  echo
  echo "## Releases Used"
  echo
  echo "- <$ZSV_TAR_URL>"
  echo "- <$TSV_TAR_URL>"
  echo "- <$XSV_TAR_URL>"
  echo
  echo '## Results'
  echo
  echo '### count'
  echo
  echo '```'
  cat "$COUNT_OUTPUT_FILE"
  echo '```'
  echo
  echo '### select'
  echo
  echo '```'
  cat "$SELECT_OUTPUT_FILE"
  echo '```'
  echo
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
