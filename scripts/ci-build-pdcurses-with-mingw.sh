#!/bin/sh

set -e

wget https://github.com/wmcbrine/PDCurses/archive/refs/tags/3.9.tar.gz
tar -xvzf 3.9.tar.gz

cd PDCurses-3.9/wincon
make CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar WIDE=Y UTF8=Y
mv pdcurses.a libpdcurses.a
rm ./*.o

cd ../..
rm 3.9.tar.gz

if [ "$CI" = true ]; then
  {
    echo "CFLAGS=-I$PWD/PDCurses-3.9"
    echo "LDFLAGS=-L$PWD/PDCurses-3.9/wincon"
  } >>"$GITHUB_ENV"
fi
