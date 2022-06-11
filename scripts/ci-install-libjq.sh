#!/bin/sh

set -e

echo "[INF] Running $0"

echo "[INF] Building and installing jq (libjq)"

echo "[INF] PWD:              $PWD"

git clone git@github.com:stedolan/jq.git
cd jq
git checkout cff5336

echo "[INF] Configuring"
autoreconf -fi
CFLAGS='-O3' ./configure \
  --disable-maintainer-mode \
  --without-oniguruma \
  --disable-shared \
  --enable-static

echo "[INF] Building and installing"
make install
cd ..

echo "[INF] Verifying"
whereis jq

echo "[INF] --- [DONE] ---"
