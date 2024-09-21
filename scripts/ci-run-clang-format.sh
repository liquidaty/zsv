#!/bin/sh

set -e

echo "[INF] Running $0"

VERSION=$(clang-format --version | sed 's/^[^0-9]*//g' | sed 's/ .*$//g')
MAJOR_VERSION=$(echo "$VERSION" | cut -d '.' -f1)
REQUIRED_VERSION="15"

if [ "$VERSION" = "" ]; then
  echo "[ERR] clang-format is not installed!"
  echo "[ERR] Make sure clang-format $REQUIRED_VERSION or later is installed."
  exit 1
else
  echo "[INF] clang-format version [$VERSION]"
  if [ "$MAJOR_VERSION" -lt "$REQUIRED_VERSION" ]; then
    echo "[ERR] Installed clang-format version is $VERSION."
    echo "[ERR] clang-format-$REQUIRED_VERSION or later is required!"
    exit 1
  fi
fi

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
