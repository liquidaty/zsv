#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set ARTIFACT_DIR before running $0 script."
  exit 1
fi

ARTIFACT_PREFIX='zsv'
if [ "$TAG" != "" ]; then
  VERSION="$("$PREFIX/bin/zsv" version | cut -d ' ' -f3 | cut -c2-)"
  ARTIFACT_PREFIX="zsv-$VERSION"
fi

echo "[INF] Preparing build artifacts for upload"

echo "[INF] TAG:              $TAG"
echo "[INF] ARTIFACT_DIR:     $ARTIFACT_DIR"
echo "[INF] ARTIFACT_PREFIX:  $ARTIFACT_PREFIX"

cd "$ARTIFACT_DIR"
for ARTIFACT_NAME in *.zip; do
  [ -e "$ARTIFACT_NAME" ] || break
  UPDATED_ARTIFACT_NAME="$ARTIFACT_PREFIX-$ARTIFACT_NAME"
  echo "[$ARTIFACT_NAME] => [$UPDATED_ARTIFACT_NAME]"
  cp "$ARTIFACT_NAME" "$UPDATED_ARTIFACT_NAME"
done
for ARTIFACT_NAME in *.tar.gz; do
  [ -e "$ARTIFACT_NAME" ] || break
  UPDATED_ARTIFACT_NAME="$ARTIFACT_PREFIX-$ARTIFACT_NAME"
  echo "[$ARTIFACT_NAME] => [$UPDATED_ARTIFACT_NAME]"
  cp "$ARTIFACT_NAME" "$UPDATED_ARTIFACT_NAME"
done
for ARTIFACT_NAME in *.deb; do
  [ -e "$ARTIFACT_NAME" ] || break
  UPDATED_ARTIFACT_NAME="$ARTIFACT_PREFIX-$ARTIFACT_NAME"
  echo "[$ARTIFACT_NAME] => [$UPDATED_ARTIFACT_NAME]"
  cp "$ARTIFACT_NAME" "$UPDATED_ARTIFACT_NAME"
done
for ARTIFACT_NAME in *.rpm; do
  [ -e "$ARTIFACT_NAME" ] || break
  UPDATED_ARTIFACT_NAME="$ARTIFACT_PREFIX-$ARTIFACT_NAME"
  echo "[$ARTIFACT_NAME] => [$UPDATED_ARTIFACT_NAME]"
  cp "$ARTIFACT_NAME" "$UPDATED_ARTIFACT_NAME"
done
cd ..
ls -Gghl "$ARTIFACT_DIR"

echo "[INF] --- [DONE] ---"
