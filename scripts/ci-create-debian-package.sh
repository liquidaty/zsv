#!/bin/sh

set -e

if [ "$PREFIX" = "" ] || [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set PREFIX and ARTIFACT_DIR before running $0 script."
  exit 1
fi

if [ ! -d "$PREFIX" ]; then
  echo "[ERR] PREFIX directory not found! [$PREFIX]"
  echo "[ERR] First build with PREFIX [$PREFIX] and then run $0."
  exit 1
fi

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "[ERR] Artifact directory not found! [$ARTIFACT_DIR]"
  exit 1
fi

ARCH="$(echo "$PREFIX" | cut -d '-' -f1)"
VERSION="$(date "+%d.%m.%y")-debug"
if [ "$TAG" != "" ]; then
  VERSION="$("$PREFIX/bin/zsv" version | cut -d ' ' -f3 | cut -c2-)"
fi

echo "[INF] ARTIFACT_DIR: $ARTIFACT_DIR"
echo "[INF] PREFIX:       $PREFIX"
echo "[INF] ARCH:         $ARCH"
echo "[INF] VERSION:      $VERSION"

DEBIAN_PKG="$PREFIX.deb"
DEBIAN_DIR="$PREFIX/DEBIAN"
DEBIAN_CONTROL_FILE="$DEBIAN_DIR/control"

echo "[INF] Creating debian package [$DEBIAN_PKG]"
mkdir -p "$PREFIX/usr"
mv -f "$PREFIX/bin" "$PREFIX/include" "$PREFIX/lib" "$PREFIX/usr"

mkdir -p "$DEBIAN_DIR"
cat << EOF > "$DEBIAN_CONTROL_FILE"
Package: zsv
Version: $VERSION
Section: extras
Priority: optional
Maintainer: Liquidaty <liquidaty@users.noreply.github.com>
Architecture: $ARCH
Description: zsv+lib: world's fastest CSV parser, with an extensible CLI
Homepage: https://github.com/liquidaty/zsv
Depends: libtinfo5
EOF

echo "[INF] Dumping $DEBIAN_CONTROL_FILE"
cat "$DEBIAN_CONTROL_FILE"

dpkg-deb --root-owner-group --build "$PREFIX"
dpkg --contents "$DEBIAN_PKG"
ls -hl "$DEBIAN_PKG"
mv "$DEBIAN_PKG" "$ARTIFACT_DIR/"

echo "[INF] --- [DONE] ---"
