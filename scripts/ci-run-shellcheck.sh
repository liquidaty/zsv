#!/bin/sh

set -e

echo "[INF] Running $0"

VERSION=$(shellcheck --version | grep 'version:' | cut -d ' ' -f2)
if [ "$VERSION" = "" ]; then
  echo "[ERR] shellcheck is not installed!"
  exit 1
fi

echo "[INF] shellcheck version [$VERSION]"

echo "[INF] Running shellcheck..."

echo "[INF] Generating tty output..."
OUTPUT_TTY="$(shellcheck --format=tty \
  configure \
  app/test/*.sh \
  scripts/*.sh \
  setup-action/scripts/*.bash 2>&1 || true)"

echo "[INF] Generating diff output..."
OUTPUT_DIFF="$(shellcheck --format=diff \
  configure \
  app/test/*.sh \
  scripts/*.sh \
  setup-action/scripts/*.bash 2>&1 || true)"

if [ "$OUTPUT_TTY" != "" ] && [ "$OUTPUT_DIFF" != "" ]; then
  echo "[ERR] Issues found!"
  echo "[ERR] Dumping tty output..."
  echo "$OUTPUT_TTY"
  echo "[ERR] Dumping diff output..."
  echo "$OUTPUT_DIFF"
else
  echo "[INF] No issues found!"
fi

if [ "$CI" = true ]; then
  echo "[INF] Generating Markdown step summary..."
  {
    if [ "$OUTPUT_TTY" != "" ] && [ "$OUTPUT_DIFF" != "" ]; then
      echo "<details>"
      echo "<summary>Shellcheck Summary (tty format)</summary>"
      echo
      echo '```'
      echo "$OUTPUT_TTY"
      echo '```'
      echo
      echo "</details>"
      echo
      echo "<details>"
      echo "<summary>Shellcheck Summary (diff format)</summary>"
      echo
      echo '```diff'
      echo "$OUTPUT_DIFF"
      echo '```'
      echo "</details>"
    else
      echo "No issues found!"
    fi
  } >>"$GITHUB_STEP_SUMMARY"
fi

echo "[INF] shellcheck ran successfully!"
echo "[INF] --- [DONE] ---"
