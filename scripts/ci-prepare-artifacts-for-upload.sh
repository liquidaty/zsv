#!/bin/bash

set -e

if [[ -z $ARTIFACT_DIR ]]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set ARTIFACT_DIR before running $0 script."
  exit 1
fi

ARTIFACT_PREFIX='zsv'
if [[ -z $TAG ]]; then
  ARTIFACT_PREFIX="zsv-$TAG"
fi

echo "[INF] TAG:              $TAG"
echo "[INF] ARTIFACT_DIR:     $ARTIFACT_DIR"
echo "[INF] ARTIFACT_PREFIX:  $ARTIFACT_PREFIX"

cd "$ARTIFACT_DIR"
for ARTIFACT_NAME in *.{zip,tar.gz}; do
  [[ -e $ARTIFACT_NAME ]] || break
  UPDATED_ARTIFACT_NAME="$ARTIFACT_PREFIX-$ARTIFACT_NAME"
  echo "[$ARTIFACT_NAME] => [$UPDATED_ARTIFACT_NAME]"
  cp "$ARTIFACT_NAME" "$UPDATED_ARTIFACT_NAME"
done
cd ..
ls -Gghl "$ARTIFACT_DIR"

echo "[INF] --- [DONE] ---"
