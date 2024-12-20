#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$HOMEBREW_TAP_DEPLOY_KEY" = "" ] || [ "$TAG" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set HOMEBREW_TAP_DEPLOY_KEY and TAG before running $0 script."
  exit 1
fi

AMD64_ARCHIVE="zsv-$TAG-amd64-macosx-gcc.tar.gz"
AMD64_URL="https://github.com/liquidaty/zsv/releases/download/v$TAG/$AMD64_ARCHIVE"
ARM64_ARCHIVE="zsv-$TAG-arm64-macosx-gcc.tar.gz"
ARM64_URL="https://github.com/liquidaty/zsv/releases/download/v$TAG/$ARM64_ARCHIVE"

HOMEBREW_TAP_REPO="git@github.com:liquidaty/homebrew-zsv.git"
HOMEBREW_TAP_DIR="homebrew-zsv"
HOMEBREW_TAP_FORMULA="formula/zsv.rb"
HOMEBREW_TAP_DEPLOY_KEY_FILE="homebrew_tap_deploy_key_file"

echo "[INF] Updating homebrew tap"

echo "[INF] PWD:                  $PWD"
echo "[INF] TAG:                  $TAG"
echo "[INF] AMD64_ARCHIVE:        $AMD64_ARCHIVE"
echo "[INF] AMD64_URL:            $AMD64_URL"
echo "[INF] ARM64_ARCHIVE:        $ARM64_ARCHIVE"
echo "[INF] ARM64_URL:            $ARM64_URL"
echo "[INF] HOMEBREW_TAP_REPO:    $HOMEBREW_TAP_REPO"
echo "[INF] HOMEBREW_TAP_DIR:     $HOMEBREW_TAP_DIR"
echo "[INF] HOMEBREW_TAP_FORMULA: $HOMEBREW_TAP_FORMULA"

echo "[INF] Downloading release archives [$AMD64_ARCHIVE, $ARM64_ARCHIVE]"
wget -q "$AMD64_URL" "$ARM64_URL"
ls -hl "$AMD64_ARCHIVE" "$ARM64_ARCHIVE"

echo "[INF] Calculating SHA256 hashes [$AMD64_ARCHIVE, $ARM64_ARCHIVE]"
AMD64_HASH=$(openssl dgst -sha256 "$AMD64_ARCHIVE" | cut -d ' ' -f2 | tr -d '\n')
ARM64_HASH=$(openssl dgst -sha256 "$ARM64_ARCHIVE" | cut -d ' ' -f2 | tr -d '\n')
rm -f "$AMD64_ARCHIVE" "$ARM64_ARCHIVE"

echo "[INF] AMD64_HASH:           $AMD64_HASH"
echo "[INF] ARM64_HASH:           $ARM64_HASH"

echo "[INF] Setting up GitHub credentials"
echo "$HOMEBREW_TAP_DEPLOY_KEY" >$HOMEBREW_TAP_DEPLOY_KEY_FILE
chmod 400 $HOMEBREW_TAP_DEPLOY_KEY_FILE
export GIT_SSH_COMMAND="ssh -i $PWD/$HOMEBREW_TAP_DEPLOY_KEY_FILE -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no"
git config --global user.name "zsv-ci"
git config --global user.email "zsv-ci@localhost"

rm -rf "$HOMEBREW_TAP_DIR"

echo "[INF] Cloning homebrew tap repository [$HOMEBREW_TAP_REPO]"
git clone "$HOMEBREW_TAP_REPO"
cd "$HOMEBREW_TAP_DIR"

echo "[INF] Updating URLs and SHA256 hashes [$HOMEBREW_TAP_FORMULA]"
sed -i -e "s|AMD64_URL = .*|AMD64_URL = '$AMD64_URL'|" "$HOMEBREW_TAP_FORMULA"
sed -i -e "s|AMD64_HASH = .*|AMD64_HASH = '$AMD64_HASH'|" "$HOMEBREW_TAP_FORMULA"
sed -i -e "s|ARM64_URL = .*|ARM64_URL = '$ARM64_URL'|" "$HOMEBREW_TAP_FORMULA"
sed -i -e "s|ARM64_HASH = .*|ARM64_HASH = '$ARM64_HASH'|" "$HOMEBREW_TAP_FORMULA"

DIFF=$(git diff "$HOMEBREW_TAP_FORMULA")
if [ "$DIFF" = "" ]; then
  echo "[INF] Homebrew tap formula is already updated."
  echo "[INF] No changes required. Exiting!"
  exit 0
fi

echo "[INF] --- git diff $HOMEBREW_TAP_FORMULA STARTS ---"
echo "$DIFF"
echo "[INF] ---- git diff $HOMEBREW_TAP_FORMULA ENDS ----"

echo "[INF] Committing and pushing changes"
git add "$HOMEBREW_TAP_FORMULA"
git commit -m "Automatic bump version to v$TAG."
git push origin main

cd ..
rm -rf "$HOMEBREW_TAP_DIR"

echo "[INF] Homebrew tap updated successfully!"

echo "[INF] --- [DONE] ---"
