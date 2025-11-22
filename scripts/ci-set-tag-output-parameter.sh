#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$CI" != true ]; then
  echo "[ERR] $0 is intended to be run only in CI environment!"
  exit 1
fi

if [ "$TAG" = "" ]; then
  echo "[INF] TAG env var is not set!"
  echo "[INF] Setting TAG from the latest release..."
  TAG="$(gh release list --repo liquidaty/zsv --limit 1 --json tagName --jq '.[].tagName')"
  echo "[INF] TAG env var set from the latest release successfully! [$TAG]"
else
  echo "[INF] TAG env var is set! [$TAG]"
fi

TAG="$(echo "$TAG" | sed 's/^v//')"
echo "[INF] TAG: $TAG"

echo "TAG=$TAG" >>"$GITHUB_OUTPUT"

echo "[INF] --- [DONE] ---"
