#!/bin/sh

# Build the Linux shared library, then use it as an external C consumer would.
set -eu
export LC_ALL=C

repo_dir=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
tmp_base=${TMPDIR:-/tmp}
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
consumer="$tmp_dir/shared-library-consumer"

cd "$repo_dir"
./configure --config-file="$tmp_dir/config.mk" --prefix="$prefix" --enable-shared --disable-termcap
"${MAKE:-make}" -C src install CONFIGFILE="$tmp_dir/config.mk" BUILD_DIR="$tmp_dir/build"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$prefix/include" \
  examples/lib/test/shared_library.c -L"$prefix/lib" -lzsv -o "$consumer"

echo '[shared] Installed libraries:'
ls -l "$prefix/lib"/libzsv.so*
dynamic_info=$(readelf -d "$consumer")
printf '%s\n' "$dynamic_info" | grep NEEDED
if ! printf '%s\n' "$dynamic_info" | grep -Eq 'NEEDED.*\[libzsv[.]so([.][^]]+)?\]'; then
  echo '[shared] Consumer does not depend on libzsv.so' >&2
  exit 1
fi

# Prove that the dynamic loader resolves the library from this test's install,
# rather than a system-wide installation or the static archive.
resolved=$(LD_LIBRARY_PATH="$prefix/lib" ldd "$consumer" | awk '$1 ~ /^libzsv[.]so([.]|$)/ && $2 == "=>" {print $3; exit}')
if [ -z "$resolved" ] || [ ! -e "$resolved" ] || \
  [ "$(readlink -f "$resolved")" != "$(readlink -f "$prefix/lib/libzsv.so")" ]; then
  echo "[shared] Unexpected library resolved: ${resolved:-not found}" >&2
  exit 1
fi
printf '[shared] Loaded library: %s -> %s\n' "$resolved" "$(readlink -f "$resolved")"
echo '[shared] Exported symbols:'
nm -D --defined-only "$resolved"
# Trace the loader while the consumer really runs, not just during ldd inspection.
if ! LD_DEBUG=libs LD_LIBRARY_PATH="$prefix/lib" "$consumer" examples/lib/test/shared_library.csv \
  2>"$tmp_dir/loader.log"; then
  cat "$tmp_dir/loader.log" >&2
  exit 1
fi
if ! grep -F "calling init: $resolved" "$tmp_dir/loader.log"; then
  echo '[shared] Loader trace did not show the expected library' >&2
  cat "$tmp_dir/loader.log" >&2
  exit 1
fi
