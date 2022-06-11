#!/bin/sh

set -e

echo "[INF] Running $0"

echo "[INF] Building and generating artifacts"

JQ_VERSION='1.6'
JQ_DIR='jq-1.6'
JQ_TAR="$JQ_DIR.tar.gz"
JQ_URL="https://github.com/stedolan/jq/releases/download/$JQ_DIR/$JQ_TAR"

echo "[INF] PWD:              $PWD"
echo "[INF] JQ_VERSION:       $JQ_VERSION"
echo "[INF] JQ_DIR:           $JQ_DIR"
echo "[INF] JQ_TAR:           $JQ_TAR"
echo "[INF] JQ_URL:           $JQ_URL"

echo "[INF] Downloading"
wget "$JQ_URL"
tar xvf "$JQ_TAR"

echo "[INF] Configuring"
cd "$JQ_DIR"
CFLAGS='-O3' ./configure \
  --disable-maintainer-mode \
  --without-oniguruma \
  --disable-shared \
  --enable-static

echo "[INF] Building and installing"
make install

echo "[INF] Verifying"
whereis jq

echo "[INF] --- [DONE] ---"
