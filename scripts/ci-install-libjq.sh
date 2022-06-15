#!/bin/sh

set -e

echo "[INF] Running $0"

echo "[INF] Building and installing jq (libjq)"

JQ_GIT_URL='https://github.com/stedolan/jq.git'
JQ_GIT_COMMIT='cff5336'
JQ_DIR="$PWD/jq"
JQ_PREFIX="$JQ_DIR/build"
JQ_INCLUDE_DIR="$JQ_PREFIX/include"
JQ_LIB_DIR="$JQ_PREFIX/lib"

echo "[INF] PWD:              $PWD"
echo "[INF] CC:               $CC"
echo "[INF] JQ_GIT_URL:       $JQ_GIT_URL"
echo "[INF] JQ_GIT_COMMIT:    $JQ_GIT_COMMIT"
echo "[INF] JQ_DIR:           $JQ_DIR"
echo "[INF] JQ_PREFIX:        $JQ_PREFIX"
echo "[INF] JQ_INCLUDE_DIR:   $JQ_INCLUDE_DIR"
echo "[INF] JQ_LIB_DIR:       $JQ_LIB_DIR"

rm -rf "$JQ_DIR"

git clone "$JQ_GIT_URL"
cd jq
git checkout "$JQ_GIT_COMMIT"

echo "[INF] Configuring"
autoreconf -fi

if [ "$CC" = 'x86_64-w64-mingw32-gcc' ]; then
  CFLAGS='-O3' ./configure \
    --prefix="$JQ_PREFIX" \
    --disable-maintainer-mode \
    --without-oniguruma \
    --disable-docs \
    --disable-shared \
    --enable-static \
    --target=win64-x86_64 \
    --host=x86_64-w64-mingw32
else
  CFLAGS='-O3' ./configure \
    --prefix="$JQ_PREFIX" \
    --disable-maintainer-mode \
    --without-oniguruma \
    --disable-docs \
    --disable-shared \
    --enable-static
fi

echo "[INF] Building and installing"
make install

cd ..

echo "[INF] --- [DONE] ---"
