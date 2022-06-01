#!/bin/bash

set -e

if [[ -z $PREFIX || -z $CC || -z $MAKE || -z $ARTIFACT_DIR ]]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set PREFIX, CC, MAKE, and ARTIFACT_DIR before running $0 script."
  exit 1
fi

if [[ $RUN_TESTS != true ]]; then
  RUN_TESTS=false
fi

echo "[INF] ARTIFACT_DIR: $ARTIFACT_DIR"
echo "[INF] PREFIX:       $PREFIX"
echo "[INF] CC:           $CC"
echo "[INF] MAKE:         $MAKE"
echo "[INF] RUN_TESTS:    $RUN_TESTS"

echo "[INF] $CC version"
"$CC" --version

echo "[INF] Configuring"
./configure --prefix="$PREFIX"

if [[ $RUN_TESTS = true ]]; then
  echo "[INF] Running tests"
  rm -rf ./build ./"$PREFIX"
  "$MAKE" test
  echo "[INF] Tests completed successfully!"
fi

echo "[INF] Building"
rm -rf ./build ./"$PREFIX"
"$MAKE" install
echo "[INF] Built successfully!"

ZIP="$PREFIX.zip"
echo "[INF] Compressing [$ZIP]"
cd "$PREFIX"
zip -r "$ZIP" .
cd ..
echo "[INF] Compressed! [$ZIP]"

TAR="$PREFIX.tar.gz"
echo "[INF] Compressing [$TAR]"
tar -czvf "$TAR" "$PREFIX"
echo "[INF] Compressed! [$TAR]"

echo "[INF] Listing"
tree -h "$PREFIX"

mkdir -p "$ARTIFACT_DIR"
mv "$PREFIX/$ZIP" "$PREFIX/$TAR" "$ARTIFACT_DIR"

echo "[INF] --- [DONE] ---"
