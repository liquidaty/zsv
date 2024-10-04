#!/bin/sh

set -e

echo "[INF] Running $0"

BENCHMARKS_DIR=".benchmarks"
mkdir -p "$BENCHMARKS_DIR"
cd "$BENCHMARKS_DIR"

CSV_URL="https://burntsushi.net/stuff/worldcitiespop_mil.csv"
CSV="$(echo "$CSV_URL" | sed 's:.*/::')"
echo "[INF] Downloading CSV file... [$CSV]"
if [ ! -f "$CSV" ]; then
  wget -q "$CSV_URL"
  echo "[INF] Downloaded successfully!"
else
  echo "[INF] Download skipped! CSV file already exists! [$CSV]"
fi

ls -Gghl "$CSV"

ZSV_TAR_URL="https://github.com/liquidaty/zsv/releases/download/v0.3.9-alpha/zsv-0.3.9-alpha-amd64-macosx-gcc.tar.gz"
TSV_TAR_URL="https://github.com/eBay/tsv-utils/releases/download/v2.2.1/tsv-utils-v2.2.1_osx-x86_64_ldc2.tar.gz"
XSV_TAR_URL="https://github.com/BurntSushi/xsv/releases/download/0.13.0/xsv-0.13.0-x86_64-apple-darwin.tar.gz"

for URL in "$ZSV_TAR_URL" "$TSV_TAR_URL" "$XSV_TAR_URL"; do
  TAR="$(echo "$URL" | sed 's:.*/::')"
  echo "[INF] Downloading... [$TAR]"
  if [ ! -f "$TAR" ]; then
    wget -q "$URL"
    echo "[INF] Downloaded successfully! [$TAR]"
  else
    echo "[INF] Download skipped! Archive already exists! [$TAR]"
  fi
done

ls -Gghl ./*.tar.gz

for TAR in *.tar.gz; do
  echo "[INF] Extracting... [$TAR]"
  tar xf "$TAR"
done

TOOLS_DIR="tools"
rm -rf ./"$TOOLS_DIR"
mkdir -p "$TOOLS_DIR"

FILES="$(find . -type f)"
for FILE in $FILES; do
  if [ -x "$FILE" ]; then
    cp "$FILE" "$TOOLS_DIR"
  fi
done

ls -Gghl "$TOOLS_DIR"

COUNT_OUTPUT_FILE="count.out"
SELECT_OUTPUT_FILE="select.out"

rm -f "$COUNT_OUTPUT_FILE" "$SELECT_OUTPUT_FILE"

RUNS=6

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

  I=1
  while [ "$I" -le "$RUNS" ]; do
    {
      printf "%d | %s : " "$I" "$TOOL"
      (time $CMD <"$CSV" >/dev/null) 2>&1 | xargs
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

  I=1
  while [ "$I" -le "$RUNS" ]; do
    {
      printf "%d | %s : " "$I" "$TOOL"
      (time $CMD <"$CSV" >/dev/null) 2>&1 | xargs
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

# GitHub Actions
if [ "$CI" = true ]; then
  echo "[INF] Generating step summary..."
  {
    cat "$MARKDOWN_OUTPUT"
  } >>"$GITHUB_STEP_SUMMARY"
  echo "[INF] Generated step summary successfully!"
fi

cd ..

echo "[INF] --- [DONE] ---"
