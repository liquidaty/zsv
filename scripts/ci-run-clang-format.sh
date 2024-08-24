#!/bin/sh

set -e

echo "[INF] Running $0"

VERSION=$(clang-format --version | cut -d ' ' -f4)
echo "[INF] clang-format version [$VERSION]"

for DIR in app examples include src; do
  echo "[INF] Running clang-format [$DIR]"
  find "$DIR" \
    ! -path "$DIR/external/*" \
    -type f \
    -regex '.*\.\(c\|h\|h\.in\)' \
    -exec sh -c 'clang-format -style=file -i "$1" &' - '{}' \;
done

wait

if ! git diff --exit-code -- '*.c' '*.h' '*.h.in'; then
  git status
  echo "[ERR] Source files are not formatted!"
  echo "[ERR] See above diff for details."
  echo "[ERR] Run clang-format and push again."
  exit 1
else
  echo "[INF] Source files are formatted!"
fi

echo "[INF] --- [DONE] ---"
