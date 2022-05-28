#!/bin/sh

set -e

if [ "$PREFIX" = "" ] || [ "$CC" = "" ] || [ "$MAKE" = "" ] || [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set PREFIX, CC, and MAKE before running $0 script."
  exit 1
fi

if [ "$RUN_TESTS" != true ]; then
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

if [ "$RUN_TESTS" = true ]; then
  echo "[INF] Running tests"
  rm -rf ./build ./"$PREFIX"
  "$MAKE" test
  echo "[INF] Tests completed successfully!"
fi

echo "[INF] Building"
rm -rf ./build ./"$PREFIX"
"$MAKE" install
echo "[INF] Built successfully!"

echo "[INF] Compressing"
cd "$PREFIX"
zip -r "$PREFIX.zip" .
cd ..
echo "[INF] Compressed! [$PREFIX.zip]"

echo "[INF] Listing"
tree -h "$PREFIX"

mkdir -p "$ARTIFACT_DIR"
mv "$PREFIX/$PREFIX.zip" "$ARTIFACT_DIR"/

echo "[INF] --- [DONE] ---"
