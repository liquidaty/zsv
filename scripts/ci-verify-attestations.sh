#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] Set ARTIFACT_DIR before running $0 script."
  exit 1
fi

for ARTIFACT in "$ARTIFACT_DIR"/*; do
  echo "[INF] Verifying attestations... [$ARTIFACT]"
  gh attestation verify "$ARTIFACT" --repo "liquidaty/zsv"
  echo "[INF] Verified successfully! [$ARTIFACT]"
done

echo "[INF] --- [DONE] ---"
