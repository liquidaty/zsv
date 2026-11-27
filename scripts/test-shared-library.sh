#!/bin/sh

# Build and install the shared library, then link and run a separate consumer.
set -eu
export LC_ALL=C

repo_dir=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp_base=$(CDPATH='' cd -- "${TMPDIR:-/tmp}" && pwd -P)
tmp_dir=$(mktemp -d "$tmp_base/zsv-shared-test.XXXXXXXX")
cleanup() {
  case "$tmp_dir" in
    "$tmp_base"/zsv-shared-test.*) rm -rf -- "$tmp_dir" ;;
    *) echo "[shared] Refusing to remove unexpected temporary path: $tmp_dir" >&2 ;;
  esac
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

prefix="$tmp_dir/install"
libdir="$prefix/lib"
consumer="$tmp_dir/shared-library-consumer"

cd "$repo_dir"
./configure --config-file="$tmp_dir/config.mk" --prefix="$prefix" --exec-prefix="$prefix" \
  --enable-shared --disable-termcap
# The standalone build uses the temporary configuration and install directories.
MAKEFLAGS='' "${MAKE:-make}" -C src install CONFIGFILE="$tmp_dir/config.mk" BUILD_DIR="$tmp_dir/build" \
  LIBDIR="$libdir" INCLUDEDIR="$prefix/include" DESTDIR=

# Link directly to the installed shared library.
platform=$(uname -s)
if [ "${OS:-}" = Windows_NT ]; then
  platform=Windows
fi
set --
case "$platform" in
  Linux)
    library="$libdir/libzsv.so"
    export LD_LIBRARY_PATH="$libdir"
    set -- -fPIC -ldl
    ;;
  Darwin)
    library="$libdir/libzsv.dylib"
    export DYLD_LIBRARY_PATH="$libdir"
    ;;
  Windows|MINGW*|MSYS*)
    library="$libdir/libzsv.dll"
    consumer="$consumer.exe"
    export PATH="$libdir:$PATH"
    ;;
  *)
    echo "[shared] Shared library test is unavailable on $platform" >&2
    exit 1
    ;;
esac

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$prefix/include" \
  examples/lib/test/shared_library.c "$library" "$@" -o "$consumer"
"$consumer" examples/lib/test/shared_library.csv "$library"
