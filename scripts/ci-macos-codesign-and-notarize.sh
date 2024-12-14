#!/bin/sh

set -e

echo "[INF] Running $0"

# Startup checks

if [ "$#" -ne 1 ] || [ "$1" = "" ]; then
  echo "[ERR] Usage: $0 [ARCHIVE.zip]"
  echo "[ERR] Following environment variables are required:"
  echo "[ERR] - MACOS_CERT_P12              (base64 encoded)"
  echo "[ERR] - MACOS_CERT_PASSWORD         (plaintext)"
  echo "[ERR] - APPLE_APP_SPECIFIC_PASSWORD (plaintext)"
  exit 1
fi

if [ "$CI" != true ] || [ "$RUNNER_OS" != "macOS" ]; then
  echo "[ERR] Must be run in GitHub Actions CI on a macOS runner!"
  exit 1
fi

# Inputs (CLI arguments + environment variables)

APP_ARCHIVE=${1:-}
APP_IDENTIFIER="com.liquidaty.zsv"
APP_TEAM_ID="HXK8Y6Q9K2"
APP_IDENTITY="Developer ID Application: matt wong ($APP_TEAM_ID)"

MACOS_CERT_P12=${MACOS_CERT_P12:-}
MACOS_CERT_PASSWORD=${MACOS_CERT_PASSWORD:-}

APPLE_ID="matt@liquidaty.com"
APPLE_APP_SPECIFIC_PASSWORD=${APPLE_APP_SPECIFIC_PASSWORD:-}

# Validations

echo "[INF] Validating arguments and environment variables"

if [ ! -f "$APP_ARCHIVE" ]; then
  echo "[ERR] Invalid archive! [$APP_ARCHIVE]"
  echo "[ERR] Archive does not exist or is not a file!"
  exit 1
elif ! echo "$APP_ARCHIVE" | grep '.zip$' >/dev/null ||
  ! file --mime "$APP_ARCHIVE" | grep 'application/zip' >/dev/null; then
  echo "[ERR] Invalid archive type! [$APP_ARCHIVE]"
  echo "[ERR] Only .zip archive is supported!"
  exit 1
fi

if [ "$MACOS_CERT_P12" = "" ]; then
  echo "[ERR] MACOS_CERT_P12 is not set!"
  exit 1
elif [ "$MACOS_CERT_PASSWORD" = "" ]; then
  echo "[ERR] MACOS_CERT_PASSWORD is not set!"
  exit 1
elif [ "$APPLE_APP_SPECIFIC_PASSWORD" = "" ]; then
  echo "[ERR] APPLE_APP_SPECIFIC_PASSWORD is not set!"
  exit 1
fi

echo "[INF] PWD                         : $PWD"
echo "[INF] APP_ARCHIVE                 : $APP_ARCHIVE"
echo "[INF] APP_IDENTIFIER              : $APP_IDENTIFIER"
echo "[INF] APP_TEAM_ID                 : $APP_TEAM_ID"
echo "[INF] APP_IDENTITY                : $APP_IDENTITY"
echo "[INF] MACOS_CERT_P12              : $MACOS_CERT_P12"
echo "[INF] MACOS_CERT_PASSWORD         : $MACOS_CERT_PASSWORD"
echo "[INF] APPLE_ID                    : $APPLE_ID"
echo "[INF] APPLE_APP_SPECIFIC_PASSWORD : $APPLE_APP_SPECIFIC_PASSWORD"

echo "[INF] Validated inputs and environment variables successfully!"

# Archive

echo "[INF] Setting up temporary directory and archive"
TMP_ARCHIVE=$(basename "$APP_ARCHIVE")
TMP_DIR="$RUNNER_TEMP/codesign-$RUNNER_ARCH-$RUNNER_OS"
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"
cp "$APP_ARCHIVE" "$TMP_DIR/$TMP_ARCHIVE"
cd "$TMP_DIR"
unzip "$TMP_ARCHIVE"
rm "$TMP_ARCHIVE"
ls -hl
echo "[INF] Set up temporary directory archive successfully!"

# Keychain + Certificate

echo "[INF] Setting up keychain and importing certificate"
KEYCHAIN="build.keychain"
CERTIFICATE="$RUNNER_TEMP/codesign-cert-$RUNNER_ARCH-$RUNNER_OS.p12"
echo "$MACOS_CERT_P12" | base64 --decode >"$CERTIFICATE"
security create-keychain -p actions "$KEYCHAIN"
security default-keychain -s "$KEYCHAIN"
security unlock-keychain -p actions "$KEYCHAIN"
security set-keychain-settings -t 3600 -u "$KEYCHAIN"
security import "$CERTIFICATE" -k "$KEYCHAIN" -P "$MACOS_CERT_PASSWORD" -A -t cert -f pkcs12 -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple: -s -k actions "$KEYCHAIN"
security find-identity -v "$KEYCHAIN"
rm "$CERTIFICATE"
echo "[INF] Set up keychain and imported certificate successfully!"

# Codesigning

echo "[INF] Codesigning"

echo "[INF] Codesigning all files and subdirectories"
find "$TMP_DIR"/bin "$TMP_DIR"/include "$TMP_DIR"/lib -type f -exec \
  codesign --verbose --deep --force --verify --options=runtime --timestamp \
  --sign "$APP_IDENTITY" --identifier "$APP_IDENTIFIER" {} +
echo "[INF] Codesigned all files and subdirectories successfully!"

echo "[INF] Creating final archive [$PWD/$TMP_ARCHIVE]"
zip -r ./"$TMP_ARCHIVE" ./bin ./include ./lib
echo "[INF] Created final archive successfully!"

echo "[INF] Codesigning final archive [$PWD/$TMP_ARCHIVE]"
codesign --verbose --force --verify --options=runtime --timestamp \
  --sign "$APP_IDENTITY" --identifier "$APP_IDENTIFIER" "$TMP_ARCHIVE"
echo "[INF] Codesigned final archive successfully!"

echo "[INF] Codesigned successfully!"

# Notarization

echo "[INF] Notarizing [$PWD/$TMP_ARCHIVE]"
NOTARIZATION_OUTPUT=$(xcrun notarytool submit "$TMP_ARCHIVE" \
  --apple-id "$APPLE_ID" \
  --password "$APPLE_APP_SPECIFIC_PASSWORD" \
  --team-id "$APP_TEAM_ID" \
  --output-format json \
  --wait)
echo "[INF] NOTARIZATION_OUTPUT: $NOTARIZATION_OUTPUT"
if ! echo "$NOTARIZATION_OUTPUT" | jq -e '.status == "Accepted"' >/dev/null; then
  echo "[ERR] Failed to notarize! See above output for errors."
  exit 1
fi
echo "[INF] Notarized successfully!"

echo "[INF] Placing final archive [$PWD/$TMP_ARCHIVE => $APP_ARCHIVE]"
cp -f "$TMP_DIR/$TMP_ARCHIVE" "$APP_ARCHIVE"
echo "[INF] Placed final archive successfully!"

# Cleanup

security delete-keychain "$KEYCHAIN"
rm -rf "$TMP_DIR"

echo "[INF] --- [DONE] ---"
