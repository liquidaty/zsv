#!/bin/bash

set -eo pipefail

echo "[INF] Setting up zsv+zsvlib..."

echo "[INF] - RUNNER_ENVIRONMENT: $RUNNER_ENVIRONMENT"
echo "[INF] - RUNNER_OS: $RUNNER_OS"
echo "[INF] - RUNNER_ARCH: $RUNNER_ARCH"
echo "[INF] - VERSION: $VERSION"

# shellcheck disable=SC2207
AVAILABLE_VERSIONS=($(git ls-remote --tags --refs https://github.com/liquidaty/zsv | cut -d '/' -f3 | sort -r | xargs))

TARGET_VERSION=
if [[ $VERSION == "latest" ]]; then
  TARGET_VERSION="${AVAILABLE_VERSIONS[0]}"
else
  if [[ $VERSION != "v"* ]]; then
    TARGET_VERSION="v$VERSION"
  fi

  echo "[INF] Validating version/tag..."
  IS_VALID_VERSION=false
  for AV in "${AVAILABLE_VERSIONS[@]}"; do
    if [[ $TARGET_VERSION == "$AV" ]]; then
      IS_VALID_VERSION=true
      break
    fi
  done
  if [[ $IS_VALID_VERSION == false ]]; then
    echo "[ERR] Version/tag not found! [$VERSION]"
    echo "[ERR] Available versions/tags are:"
    for AV in "${AVAILABLE_VERSIONS[@]}"; do
      echo "[ERR] - $AV"
    done
    exit 1
  fi
  echo "[INF] Validated version/tag successfully!"
fi

TARGET_VERSION="${TARGET_VERSION:1}"

TRIPLET=
if [[ $RUNNER_OS == "Linux" ]]; then
  if [[ $RUNNER_ARCH == "X64" ]]; then
    TRIPLET="amd64-linux-gcc"
  fi
elif [[ $RUNNER_OS == "macOS" ]]; then
  if [[ $RUNNER_ARCH == "X64" ]]; then
    TRIPLET="amd64-macosx-gcc"
  elif [[ $RUNNER_ARCH == "ARM64" ]]; then
    TRIPLET="arm64-macosx-gcc"
  fi
elif [[ $RUNNER_OS == "Windows" ]]; then
  if [[ $RUNNER_ARCH == "X86" || $RUNNER_ARCH == "X64" ]]; then
    TRIPLET="amd64-windows-mingw"
  fi

  if ! which wget >/dev/null; then
    echo "[INF] Installing wget..."
    if ! choco install wget --no-progress >/dev/null; then
      echo "[ERR] Failed to install wget!"
      exit 1
    fi
  fi
fi

if [[ -z $TRIPLET ]]; then
  echo "[ERR] Architecture/OS not supported! [$RUNNER_ARCH $RUNNER_OS]"
  exit 1
fi

INSTALL_DIR="$RUNNER_TEMP/zsv"
echo "[INF] INSTALL_DIR: $INSTALL_DIR"

rm -rf "${INSTALL_DIR:?}"/{bin,include,lib}
mkdir -p "$INSTALL_DIR"
cd "$INSTALL_DIR"

ZIP="zsv-$TARGET_VERSION-$TRIPLET.zip"
URL="https://github.com/liquidaty/zsv/releases/download/v$TARGET_VERSION/$ZIP"

echo "[INF] Downloading... [$URL]"
if [[ ! -f $ZIP ]]; then
  wget --quiet "$URL"
  echo "[INF] Downloaded successfully!"
else
  echo "[INF] Archive already exists! Skipping download..."
fi

echo "[INF] Extracting... [$ZIP]"
unzip -q -o "$ZIP"
echo "[INF] Extracted successfully!"

INSTALL_PATH="$(realpath "$INSTALL_DIR")"
echo "[INF] INSTALL_PATH: $INSTALL_PATH"

echo "[INF] Setting PATH... [$INSTALL_PATH]"
echo "$INSTALL_PATH/bin" >>"$GITHUB_PATH"

echo "[INF] Setting output parameter... [install-path]"
echo "install-path=$INSTALL_PATH" >>"$GITHUB_OUTPUT"

echo "[INF] zsv+zsvlib set up successfully!"
echo "[INF] --- [DONE] ---"
