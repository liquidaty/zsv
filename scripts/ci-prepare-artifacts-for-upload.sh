#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set ARTIFACT_DIR before running $0 script."
  exit 1
fi

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "[ERR] Artifact directory not found! [$ARTIFACT_DIR]"
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

prepare() {
  [ "$1" = "" ] && return
  for ARTIFACT_NAME in *."$1"; do
    [ -e "$ARTIFACT_NAME" ] || break
    UPDATED_ARTIFACT_NAME="$ARTIFACT_PREFIX-$ARTIFACT_NAME"
    FILE_PREFIX="$(echo "$ARTIFACT_NAME" | cut -c -${#ARTIFACT_PREFIX})"
    [ "$FILE_PREFIX" = "$ARTIFACT_PREFIX" ] && continue
    echo "[INF] [$ARTIFACT_NAME] => [$UPDATED_ARTIFACT_NAME]"
    cp "$ARTIFACT_NAME" "$UPDATED_ARTIFACT_NAME"
  done
}

cd "$ARTIFACT_DIR"

prepare 'zip'
prepare 'tar.gz'
prepare 'deb'
prepare 'rpm'
prepare 'nupkg'

cd ..

echo "[INF] Listing"
ls -Gghl "$ARTIFACT_DIR"

echo "[INF] --- [DONE] ---"
