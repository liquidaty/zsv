#!/bin/sh

set -e

if [ "$PREFIX" = "" ] || [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set PREFIX and ARTIFACT_DIR before running $0 script."
  exit 1
fi

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "[ERR] Artifact directory not found! [$ARTIFACT_DIR]"
  exit 1
fi

VERSION="$(zsv version | cut -d ' ' -f3 | cut -c2-)"

echo "[INF] ARTIFACT_DIR: $ARTIFACT_DIR"
echo "[INF] PREFIX:       $PREFIX"
echo "[INF] VERSION:      $VERSION"

DEB="$PREFIX.deb"
echo "[INF] Creating debian package [$DEB]"
mkdir -p "$PREFIX/DEBIAN"

cat << EOF > "$PREFIX/DEBIAN/control"
Package: zsv
Version: $VERSION
Maintainer: Liquidaty <liquidaty@users.noreply.github.com>
Architecture: amd64
Description: zsv+lib: world's fastest CSV parser, with an extensible CLI
Homepage: https://github.com/liquidaty/zsv
EOF

dpkg-deb --root-owner-group --build "$PREFIX"
dpkg --contents "$DEB"
ls -hl "$DEB"
mv "$DEB" "$ARTIFACT_DIR/"

echo "[INF] --- [DONE] ---"
