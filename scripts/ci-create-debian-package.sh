#!/bin/sh

set -e

echo "[INF] Running $0"

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
VERSION="$(date "+%d.%m.%y").$(date "+%s")"
if [ "$TAG" != "" ]; then
  VERSION="$("$PREFIX/bin/zsv" version | cut -d ' ' -f3 | cut -c2-)"
fi

DEBIAN_PKG="$PREFIX.deb"
DEBIAN_DIR="$PREFIX/DEBIAN"
DEBIAN_CONTROL_FILE="$DEBIAN_DIR/control"
DEBIAN_PREINST_SCRIPT="$DEBIAN_DIR/preinst"

echo "[INF] Creating debian package [$DEBIAN_PKG]"

echo "[INF] PWD:          $PWD"
echo "[INF] ARTIFACT_DIR: $ARTIFACT_DIR"
echo "[INF] PREFIX:       $PREFIX"
echo "[INF] ARCH:         $ARCH"
echo "[INF] VERSION:      $VERSION"

mkdir -p "$DEBIAN_DIR" "$PREFIX/usr"
mv -f "$PREFIX/bin" "$PREFIX/include" "$PREFIX/lib" "$PREFIX/usr/"

echo "[INF] Creating control file [$DEBIAN_CONTROL_FILE]"

INSTALLED_SIZE="$(echo $(du -s -c $PREFIX/usr/* | grep 'total') | cut -d ' ' -f1)"
cat << EOF > "$DEBIAN_CONTROL_FILE"
Package: zsv
Version: $VERSION
Section: extras
Priority: optional
Maintainer: Liquidaty <liquidaty@users.noreply.github.com>
Architecture: $ARCH
Description: zsv+lib: world's fastest CSV parser, with an extensible CLI
Homepage: https://github.com/liquidaty/zsv
Installed-Size: $INSTALLED_SIZE
Depends: libtinfo5
EOF

ls -Gghl "$DEBIAN_CONTROL_FILE"

echo "[INF] Dumping [$DEBIAN_CONTROL_FILE]"
echo "[INF] --- [$DEBIAN_CONTROL_FILE] ---"
cat "$DEBIAN_CONTROL_FILE"
echo "[INF] --- [$DEBIAN_CONTROL_FILE] ---"

echo "[INF] Creating preinst script [$DEBIAN_CONTROL_FILE]"

cat << EOF > "$DEBIAN_PREINST_SCRIPT"
#!/bin/sh

rm -rf '/usr/bin/zsv' '/usr/lib/libzsv.a' '/usr/include/zsv.h' '/usr/include/zsv'
EOF

chmod +x "$DEBIAN_PREINST_SCRIPT"
ls -Gghl "$DEBIAN_PREINST_SCRIPT"

echo "[INF] Dumping [$DEBIAN_PREINST_SCRIPT]"
echo "[INF] --- [$DEBIAN_PREINST_SCRIPT] ---"
cat "$DEBIAN_PREINST_SCRIPT"
echo "[INF] --- [$DEBIAN_PREINST_SCRIPT] ---"

echo "[INF] Building debian package"
dpkg-deb --root-owner-group --build "$PREFIX"
dpkg --contents "$DEBIAN_PKG"
ls -Gghl "$DEBIAN_PKG"
mv -f "$DEBIAN_PKG" "$ARTIFACT_DIR/"

mv -f "$PREFIX/usr/bin" "$PREFIX/usr/lib" "$PREFIX/usr/include" "$PREFIX/"
rm -rf "./$PREFIX/DEBIAN" "./$PREFIX/usr"

echo "[INF] Verifying debian package [$ARTIFACT_DIR/$DEBIAN_PKG]"
sudo apt install -y "./$ARTIFACT_DIR/$DEBIAN_PKG"
whereis zsv
zsv version
sudo apt remove -y zsv

echo "[INF] --- [DONE] ---"
