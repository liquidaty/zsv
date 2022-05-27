#!/bin/sh

set -e

if [ "$PREFIX" = "" ] || [ "$CC" = "" ] || [ "$MAKE" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set PREFIX, CC, and MAKE before running $0 script."
  exit 1
fi

echo "PREFIX: $PREFIX"
echo "CC:     $CC"
echo "MAKE:   $MAKE"

echo "Building [$PREFIX]"

rm -rf ./build ./$PREFIX

"$CC" --version
./configure --prefix="$PREFIX"
"$MAKE" install

echo "Compressing"
cd "$PREFIX"
zip -r "$PREFIX.zip" .
cd ..

echo "Listing"
tree -h "$PREFIX"
mv "$PREFIX/$PREFIX.zip" .

echo "--- [DONE] ---"
