#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$TAG" = "" ] || [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] Set TAG and ARTIFACT_DIR before running $0 script."
  exit 1
fi

echo "[INF] Listing release artifacts"
ls -hl "$ARTIFACT_DIR/zsv-$TAG-"*

for ARTIFACT in "$ARTIFACT_DIR/zsv-$TAG-"*; do
  if [ -f "$ARTIFACT" ]; then
    echo "[INF] Uploading artifact... [$ARTIFACT]"
    gh release upload "$GITHUB_REF_NAME" "$ARTIFACT"
    echo "[INF] Artifact uploaded successfully! [$ARTIFACT]"
  fi
done

echo "[INF] --- [DONE] ---"
