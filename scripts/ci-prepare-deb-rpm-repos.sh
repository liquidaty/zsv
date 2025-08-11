#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$TAG" = "" ] || [ "$ARTIFACT_DIR" = "" ] || [ "$AMD64_ZIP" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set TAG, ARTIFACT_DIR and AMD64_ZIP before running $0 script."
  exit 1
fi

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "[ERR] $ARTIFACT_DIR directory not found!"
  exit 1
fi

if [ ! -f "$ARTIFACT_DIR/$AMD64_ZIP" ]; then
  echo "[ERR] $ARTIFACT_DIR/$AMD64_ZIP file not found!"
  echo "[ERR] Make sure the artifact is downloaded before running $0 script."
  exit 1
fi

if [ ! -f ".fpm" ]; then
  echo "[ERR] .fpm file not found!"
  exit 1
fi

AMD64_PKG=$(basename --suffix .zip "$AMD64_ZIP")
AMD64_DIR="$AMD64_PKG"
AMD64_DEB_CLI="zsv-$TAG-amd64-linux.deb"
AMD64_DEB_LIB="zsv-dev-$TAG-amd64-linux.deb"
AMD64_RPM_CLI="zsv-$TAG-amd64-linux.rpm"
AMD64_RPM_LIB="zsv-devel-$TAG-amd64-linux.rpm"

echo "[INF] TAG:            $TAG"
echo "[INF] ARTIFACT_DIR:   $ARTIFACT_DIR"
echo "[INF] AMD64_ZIP:      $AMD64_ZIP"
echo "[INF] AMD64_PKG:      $AMD64_PKG"
echo "[INF] AMD64_DIR:      $AMD64_DIR"
echo "[INF] AMD64_DEB_CLI:  $AMD64_DEB_CLI"
echo "[INF] AMD64_DEB_LIB:  $AMD64_DEB_LIB"
echo "[INF] AMD64_RPM_CLI:  $AMD64_RPM_CLI"
echo "[INF] AMD64_RPM_LIB:  $AMD64_RPM_LIB"

echo "[INF] Setting up directories"
cd "$ARTIFACT_DIR"
rm -rf "$AMD64_DIR" "$AMD64_DEB_CLI" "$AMD64_RPM_CLI"
mkdir -p "$AMD64_DIR"

echo "[INF] Unzipping the artifact [$AMD64_ZIP]"
unzip -o "$AMD64_ZIP" -d "$AMD64_DIR"

echo "[INF] Building DEB package (exe)"
fpm \
  --fpm-options-file ../.fpm \
  --input-type dir \
  --output-type deb \
  --name zsv \
  --description "zsv CLI - tabular data swiss-army knife" \
  --version "$TAG" \
  --package "$AMD64_DEB_CLI" \
  "$AMD64_PKG/bin/zsv=/usr/local/bin/zsv"
dpkg --info "$AMD64_DEB_CLI"
dpkg --contents "$AMD64_DEB_CLI"
ls -hl "$AMD64_DEB_CLI"

echo "[INF] Building DEB package (lib)"
fpm \
  --fpm-options-file ../.fpm \
  --input-type dir \
  --output-type deb \
  --name zsv-dev \
  --description "zsv lib (static) - world's fastest (simd) CSV parser" \
  --version "$TAG" \
  --package "$AMD64_DEB_LIB" \
  "$AMD64_PKG/lib/libzsv.a=/usr/local/lib/libzsv.a" \
  "$AMD64_PKG/include/zsv.h=/usr/local/include/zsv.h" \
  "$AMD64_PKG/include/zsv=/usr/local/include/"
dpkg --info "$AMD64_DEB_LIB"
dpkg --contents "$AMD64_DEB_LIB"
ls -hl "$AMD64_DEB_LIB"

echo "[INF] Building RPM package (exe)"
fpm \
  --fpm-options-file ../.fpm \
  --input-type dir \
  --output-type rpm \
  --name zsv \
  --description "zsv CLI - tabular data swiss-army knife" \
  --version "$TAG" \
  --package "$AMD64_RPM_CLI" \
  "$AMD64_PKG/bin/zsv=/usr/local/bin/zsv"
rpm -qip "$AMD64_RPM_CLI"
rpm -qlp "$AMD64_RPM_CLI"
ls -hl "$AMD64_RPM_CLI"

echo "[INF] Building RPM package (lib)"
fpm \
  --fpm-options-file ../.fpm \
  --input-type dir \
  --output-type rpm \
  --name zsv-devel \
  --description "zsv lib (static) - world's fastest (simd) CSV parser" \
  --version "$TAG" \
  --package "$AMD64_RPM_LIB" \
  "$AMD64_PKG/lib/libzsv.a=/usr/local/lib/libzsv.a" \
  "$AMD64_PKG/include/zsv.h=/usr/local/include/zsv.h" \
  "$AMD64_PKG/include/zsv=/usr/local/include/"
rpm -qip "$AMD64_RPM_LIB"
rpm -qlp "$AMD64_RPM_LIB"
ls -hl "$AMD64_RPM_LIB"

rm -rf ./packages

echo "[INF] Copying deb and rpm files to packages directory"
mkdir -p packages/apt/amd64
cp "$AMD64_DEB_CLI" "$AMD64_DEB_LIB" packages/apt/amd64/
ls -hl packages/apt/amd64

mkdir -p packages/rpm/amd64
cp "$AMD64_RPM_CLI" "$AMD64_RPM_LIB" packages/rpm/amd64/
ls -hl packages/rpm/amd64

echo "[INF] Updating APT repository"
cd packages/apt/amd64
dpkg-scanpackages . /dev/null > Packages
gzip -k -f Packages
apt-ftparchive release . > Release
cd -

echo "[INF] Updating RPM repository"
createrepo_c packages/rpm/amd64

echo "[INF] --- [DONE] ---"
