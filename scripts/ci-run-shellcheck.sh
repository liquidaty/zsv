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

shellcheck --format=tty \
  configure \
  app/test/*.sh \
  scripts/*.sh \
  setup-action/scripts/*.bash || true

if [ "$CI" = true ]; then
  {
    echo "<details>"
    echo "<summary>Shellcheck Summary (tty format)</summary>"
    echo
    echo '```'
    shellcheck --format=tty \
      configure \
      app/test/*.sh \
      scripts/*.sh \
      setup-action/scripts/*.bash \
      2>&1 || true
    echo '```'
    echo "</details>"
    echo
    echo "<details>"
    echo "<summary>Shellcheck Summary (diff format)</summary>"
    echo
    echo '```diff'
    shellcheck --format=diff \
      configure \
      app/test/*.sh \
      scripts/*.sh \
      setup-action/scripts/*.bash \
      2>&1 || true
    echo '```'
    echo "</details>"
  } >>"$GITHUB_STEP_SUMMARY"
fi

echo "[INF] shellcheck ran successfully!"
echo "[INF] --- [DONE] ---"
