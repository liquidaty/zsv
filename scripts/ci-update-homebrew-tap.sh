#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$HOMEBREW_TAP_DEPLOY_KEY" = "" ] || [ "$TAG" = "" ] || [ "$TRIPLET" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set HOMEBREW_TAP_DEPLOY_KEY, TAG, and TRIPLET before running $0 script."
  exit 1
fi

TAR="zsv-$TAG-$TRIPLET.tar.gz"
TAR_URL="https://github.com/liquidaty/zsv/releases/download/v$TAG/$TAR"
HOMEBREW_TAP_REPO="git@github.com:liquidaty/homebrew-zsv.git"
HOMEBREW_TAP_DIR="homebrew-zsv"
HOMEBREW_TAP_FORMULA="formula/zsv.rb"
HOMEBREW_TAP_DEPLOY_KEY_FILE="homebrew_tap_deploy_key_file"

echo "[INF] Updating homebrew tap"

echo "[INF] PWD:                  $PWD"
echo "[INF] TAG:                  $TAG"
echo "[INF] TAR:                  $TAR"
echo "[INF] TAR_URL:              $TAR_URL"
echo "[INF] HOMEBREW_TAP_REPO:    $HOMEBREW_TAP_REPO"
echo "[INF] HOMEBREW_TAP_DIR:     $HOMEBREW_TAP_DIR"
echo "[INF] HOMEBREW_TAP_FORMULA: $HOMEBREW_TAP_FORMULA"

echo "[INF] Downloading release tar file [$TAR_URL]"
wget -q "$TAR_URL"
ls -Gghl "$TAR"

echo "[INF] Calculating SHA256 of $TAR"
SHA256=$(openssl dgst -sha256 "$TAR" | cut -d ' ' -f2 | tr -d '\n')
echo "[INF] SHA256: $SHA256"
rm -f "$TAR"

echo "[INF] Setting up GitHub credentials"
echo "$HOMEBREW_TAP_DEPLOY_KEY" > $HOMEBREW_TAP_DEPLOY_KEY_FILE
chmod 400 $HOMEBREW_TAP_DEPLOY_KEY_FILE
export GIT_SSH_COMMAND="ssh -i $PWD/$HOMEBREW_TAP_DEPLOY_KEY_FILE -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no"
git config --global user.name "liquidaty/zsv CI"
git config --global user.email "liquidaty/zsv-ci@localhost"

rm -rf "$HOMEBREW_TAP_DIR"

git clone "$HOMEBREW_TAP_REPO"
cd "$HOMEBREW_TAP_DIR"

# Update url and sha256
sed -i -e "s|url .*|url '$TAR_URL'|" $HOMEBREW_TAP_FORMULA
sed -i -e "s|sha256 .*|sha256 '$SHA256'|" $HOMEBREW_TAP_FORMULA

echo "[INF] --- $HOMEBREW_TAP_FORMULA STARTS ---"
cat "$HOMEBREW_TAP_FORMULA"
echo "[INF] ---- $HOMEBREW_TAP_FORMULA ENDS ----"

git add "$HOMEBREW_TAP_FORMULA"
git commit -m "Update liquidaty/homebrew-zsv tap."
git push origin main

cd ..
rm -rf "$HOMEBREW_TAP_DIR"

echo "[INF] liquidaty/homebrew-zsv tap updated successfully!"

echo "[INF] --- [DONE] ---"