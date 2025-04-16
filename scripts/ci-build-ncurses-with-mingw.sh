#!/bin/sh

set -e

wget https://ftp.gnu.org/gnu/ncurses/ncurses-6.5.tar.gz
tar -xvzf ncurses-6.5.tar.gz

cd ncurses-6.5

./configure \
  --host=x86_64-w64-mingw32 \
  --prefix="$PWD/mingw-ncurses" \
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

make
make install

cd mingw-ncurses

zip -r mingw-ncurses.zip include lib
