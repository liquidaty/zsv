#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$TAG" = "" ] || [ "$AMD64_ZIP" = "" ] || [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set TAG, AMD64_ZIP and ARTIFACT_DIR before running $0 script."
  exit 1
fi

if [ ! -f "$AMD64_ZIP" ]; then
  echo "[ERR] $AMD64_ZIP file not found!"
  echo "[ERR] Make sure the artifact is downloaded before running $0 script."
  exit 1
fi

if [ ! -f ".fpm" ]; then
  echo "[ERR] .fpm file not found!"
  exit 1
fi

AMD64_DEB="zsv_${TAG}_amd64.deb"
AMD64_RPM="zsv-$(echo "$TAG" | tr '-' '_')-1.x86_64.rpm"

echo "[INF] TAG:          $TAG"
echo "[INF] AMD64_ZIP:    $AMD64_ZIP"
echo "[INF] ARTIFACT_DIR: $ARTIFACT_DIR"
echo "[INF] AMD64_DEB:    $AMD64_DEB"
echo "[INF] AMD64_RPM:    $AMD64_RPM"

cd "$ARTIFACT_DIR"

echo "[INF] Building DEB package"
fpm \
  --fpm-options-file ../.fpm \
  --architecture "amd64" \
  --version "$TAG" \
  --force \
  "$AMD64_ZIP"
dpkg --info "$AMD64_DEB"
dpkg --contents "$AMD64_DEB"
ls -hl "$AMD64_DEB"

echo "[INF] Building RPM package"
fpm \
  --fpm-options-file ../.fpm \
  --output-type rpm \
  --architecture "amd64" \
  --version "$TAG" \
  --force \
  "$AMD64_ZIP"
rpm -qip "$AMD64_RPM"
rpm -qlp "$AMD64_RPM"
ls -hl "$AMD64_RPM"

rm -rf ./packages

echo "[INF] Moving deb and rpm files to packages directory"
mkdir -p packages/apt/amd64
mv "$AMD64_DEB" packages/apt/amd64/
ls -hl packages/apt/amd64

mkdir -p packages/rpm/amd64
mv "$AMD64_RPM" packages/rpm/amd64/
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
