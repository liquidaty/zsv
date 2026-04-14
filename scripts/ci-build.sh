#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$PREFIX" = "" ] || [ "$CC" = "" ] || [ "$MAKE" = "" ] || [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set PREFIX, CC, MAKE, and ARTIFACT_DIR before running $0 script."
  exit 1
fi

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "[WRN] Artifact directory not found! [$ARTIFACT_DIR]"
  echo "[WRN] Artifact directory will be created!"
fi

if [ "$RUN_TESTS" != true ]; then
  RUN_TESTS=false
fi

if [ "$SKIP_BUILD" != true ]; then
  SKIP_BUILD=false
fi

if [ "$SKIP_ZIP_ARCHIVE" != true ]; then
  SKIP_ZIP_ARCHIVE=false
fi

if [ "$SKIP_TAR_ARCHIVE" != true ]; then
  SKIP_TAR_ARCHIVE=false
fi

WITHOUT_SIMD=${WITHOUT_SIMD:-false}
CMP=${CMP:-cmp}

echo "[INF] Building and generating artifacts"

echo "[INF] PWD:              $PWD"
echo "[INF] PREFIX:           $PREFIX"
echo "[INF] CC:               $CC"
echo "[INF] CFLAGS:           $CFLAGS"
echo "[INF] LDFLAGS:          $LDFLAGS"
echo "[INF] MAKE:             $MAKE"
echo "[INF] CMP:              $CMP"
echo "[INF] RUN_TESTS:        $RUN_TESTS"
echo "[INF] STATIC_BUILD:     $STATIC_BUILD"
echo "[INF] ARTIFACT_DIR:     $ARTIFACT_DIR"
echo "[INF] WITHOUT_SIMD:     $WITHOUT_SIMD"
echo "[INF] SKIP_BUILD:       $SKIP_BUILD"
echo "[INF] SKIP_ZIP_ARCHIVE: $SKIP_ZIP_ARCHIVE"
echo "[INF] SKIP_TAR_ARCHIVE: $SKIP_TAR_ARCHIVE"

echo "[INF] Listing compiler version [$CC]"
"$CC" --version

echo "[INF] Configuring zsv"

if [ "$WITHOUT_SIMD" = true ]; then
  ./configure \
    --prefix="$PREFIX" \
    --disable-termcap \
    --arch=none \
    --try-avx512=no \
    --force-avx2=no \
    --force-avx=no \
    --force-sse2=no
else
  ./configure \
    --prefix="$PREFIX" \
    --disable-termcap
  # --enable-jq
fi

if [ "$RUN_TESTS" = true ]; then
  echo "[INF] Running tests"
  rm -rf "$PREFIX"
  $MAKE clean test
  echo "[INF] Tests completed successfully!"
fi

if [ "$SKIP_BUILD" = false ]; then
  echo "[INF] Building"
  rm -rf "$PREFIX" /usr/local/etc/zsv.ini
  "$MAKE" clean install
  tree "$PREFIX"
  echo "[INF] Built successfully!"

  mkdir -p "$ARTIFACT_DIR"

  if [ "$SKIP_ZIP_ARCHIVE" = false ]; then
    ZIP="$PREFIX.zip"
    echo "[INF] Compressing [$ZIP]"
    cd "$PREFIX"
    zip -r "$ZIP" .
    ls -hl "$ZIP"
    cd ..
    mv "$PREFIX/$ZIP" "$ARTIFACT_DIR"
    echo "[INF] Compressed! [$ZIP]"
  fi

  if [ "$SKIP_TAR_ARCHIVE" = false ]; then
    TAR="$PREFIX.tar.gz"
    echo "[INF] Compressing [$TAR]"
    tar -czvf "$TAR" "$PREFIX"
    ls -hl "$TAR"
    mv "$TAR" "$ARTIFACT_DIR"
    echo "[INF] Compressed! [$TAR]"
  fi
fi

echo "[INF] --- [DONE] ---"
