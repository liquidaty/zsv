#!/bin/sh

set -e

if [ ! -f ncurses-6.5.tar.gz ]; then
  echo "[INF] Downloading ncurses-6.5.tar.gz"
  wget https://ftp.gnu.org/gnu/ncurses/ncurses-6.5.tar.gz
  tar -xvzf ncurses-6.5.tar.gz
fi

cd ncurses-6.5

NCURSES_PREFIX=${NCURSES_PREFIX:-"$PWD/mingw-ncurses"}
CC=${CC:-x86_64-w64-mingw32-gcc}

MAKE_PARALLEL=${MAKE_PARALLEL:-false}
MAKE_FLAGS=
if [ "$MAKE_PARALLEL" = true ]; then
  MAKE_FLAGS="--jobs --output-sync"
fi

ENABLE_CCACHE=${ENABLE_CCACHE:-false}
CCACHE=
if [ "$ENABLE_CCACHE" = true ]; then
  CCACHE=ccache
  echo "[INF] Using ccache for compilation"
  ccache --version
fi

./configure \
  --host=x86_64-w64-mingw32 \
  --prefix="$NCURSES_PREFIX" \
  --enable-widec \
  --enable-term-driver \
  --with-static \
  --without-debug \
  --without-shared \
  --without-manpages \
  --without-tests \
  --without-progs \
  --without-cxx \
  --without-cxx-binding \
  --without-ada \
  --without-pkg-config \
  --without-curses-h \
  --disable-pc-files \
  --disable-home-terminfo \
  --disable-termcap \
  --disable-db-install

# shellcheck disable=SC2086
make $MAKE_FLAGS CC="$CCACHE $CC"
make install

if [ "$CI" = true ]; then
  zip -r mingw-ncurses.zip mingw-ncurses/include mingw-ncurses/lib
  mv mingw-ncurses.zip "$GITHUB_WORKSPACE"
fi
