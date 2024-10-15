#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] Set ARTIFACT_DIR before running $0 script."
  exit 1
fi

for ARTIFACT in "$ARTIFACT_DIR"/*; do
  if [ -f "$ARTIFACT" ]; then
    echo "[INF] Uploading artifact... [$ARTIFACT]"
    gh release upload "$GITHUB_REF_NAME" "$ARTIFACT"
    echo "[INF] Artifact uploaded successfully! [$ARTIFACT]"
  fi
done

echo "[INF] --- [DONE] ---"
