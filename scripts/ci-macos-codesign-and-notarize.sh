#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$CI" = true ] && [ "$RUNNER_OS" != "macOS" ]; then
  echo "[ERR] Invalid OS! [$RUNNER_OS]"
  echo "[ERR] Must be run on macOS in CI!"
  exit 1
fi

MACOS_CERT_P12=${MACOS_CERT_P12:-}
MACOS_CERT_PASSWORD=${MACOS_CERT_PASSWORD:-}

APP_BUNDLE=${APP_BUNDLE:-"zsv.zip"}
APP_IDENTIFIER=${APP_IDENTIFIER:-"com.liquidaty.zsv"}

TEAM_ID=${TEAM_ID:-"HXK8Y6Q9K2"}
APP_IDENTITY="Developer ID Application: matt wong ($TEAM_ID)"

APPLE_ID="matt@liquidaty.com"
APPLE_APP_SPECIFIC_PASSWORD=${APPLE_APP_SPECIFIC_PASSWORD:-}

ENTITLEMENTS="entitlements.plist"

# Validations

echo "[INF] Validating required values"

if [ "$MACOS_CERT_P12" = "" ]; then
  echo "[ERR] MACOS_CERT_P12 is not set!"
  exit 1
elif [ "$MACOS_CERT_PASSWORD" = "" ]; then
  echo "[ERR] MACOS_CERT_PASSWORD is not set!"
  exit 1
elif [ "$APP_BUNDLE" = "" ]; then
  echo "[ERR] APP_BUNDLE is not set!"
  exit 1
elif [ "$APP_BUNDLE" != "" ] && [ ! -f "$APP_BUNDLE" ]; then
  echo "[ERR] APP_BUNDLE does not exist! [$APP_BUNDLE]"
  exit 1
elif [ "$APP_IDENTIFIER" = "" ]; then
  echo "[ERR] APP_IDENTIFIER is not set!"
  exit 1
elif [ "$TEAM_ID" = "" ]; then
  echo "[ERR] TEAM_ID is not set!"
  exit 1
elif [ "$APPLE_APP_SPECIFIC_PASSWORD" = "" ]; then
  echo "[ERR] APPLE_APP_SPECIFIC_PASSWORD is not set!"
  exit 1
elif [ ! -f "$ENTITLEMENTS" ]; then
  echo "[ERR] ENTITLEMENTS does not exist! [$ENTITLEMENTS]"
  exit 1
fi

echo "[INF] MACOS_CERT_P12              : $MACOS_CERT_P12"
echo "[INF] MACOS_CERT_PASSWORD         : $MACOS_CERT_PASSWORD"
echo "[INF] APP_BUNDLE                  : $APP_BUNDLE"
echo "[INF] APP_IDENTIFIER              : $APP_IDENTIFIER"
echo "[INF] TEAM_ID                     : $TEAM_ID"
echo "[INF] APP_IDENTITY                : $APP_IDENTITY"
echo "[INF] APPLE_ID                    : $APPLE_ID"
echo "[INF] APPLE_APP_SPECIFIC_PASSWORD : $APPLE_APP_SPECIFIC_PASSWORD"
echo "[INF] ENTITLEMENTS                : $ENTITLEMENTS"
echo "[INF] openssl version             : $(openssl version)"

echo "[INF] Validated required values successfully!"

# Keychain + Certificate

echo "[INF] Setting up keychain and importing certificate"

KEYCHAIN="build.keychain"
CERTIFICATE="macos-codesign-cert.p12"
echo "$MACOS_CERT_P12" | base64 --decode >"$CERTIFICATE"
security create-keychain -p actions "$KEYCHAIN"
security default-keychain -s "$KEYCHAIN"
security unlock-keychain -p actions "$KEYCHAIN"
security set-keychain-settings -t 3600 -u "$KEYCHAIN"
if ! security import "$CERTIFICATE" -k "$KEYCHAIN" -P "$MACOS_CERT_PASSWORD" -A -t cert -f pkcs12 -T /usr/bin/codesign; then
  openssl pkcs12 -in "$CERTIFICATE" -nocerts -out "codesign.key" -nodes -password pass:"$MACOS_CERT_PASSWORD"
  openssl pkcs12 -in "$CERTIFICATE" -clcerts -nokeys -out "codesign.crt" -password pass:"$MACOS_CERT_PASSWORD"
  # TODO: Remove
  ls -hl codesign.key codesign.crt
  security import "codesign.key" -k "$KEYCHAIN" -P "" -A -T /usr/bin/codesign
  security import "codesign.crt" -k "$KEYCHAIN" -P "" -A -T /usr/bin/codesign
fi
security set-key-partition-list -S apple-tool:,apple: -s -k actions "$KEYCHAIN"
security find-identity -v "$KEYCHAIN"

echo "[INF] Set up keychain and imported certificate successfully!"

# TODO: Set up zsv directory

# Codesigning

echo "[INF] Codesigning nested content"

find "$ZSV_ROOT" -type f -exec \
  codesign --verbose --deep --force --verify --options=runtime --timestamp \
  --sign "$APP_IDENTITY" --identifier "$APP_IDENTIFIER" "$APP_BUNDLE" {} +

echo "[INF] Codesigned nested content successfully!"

echo "[INF] Codesigning bundle"

codesign --verbose --force --verify --options=runtime --timestamp \
  --sign "$APP_IDENTITY" --identifier "$APP_IDENTIFIER" "$APP_BUNDLE"

# TODO: Set up entitlements
# --entitlements "$ENTITLEMENTS"

echo "[INF] Codesigned bundle successfully!"

# Notarization

echo "[INF] Notarizing"

xcrun notarytool submit "$APP_BUNDLE" \
  --apple-id "$APPLE_ID" \
  --password "$APPLE_APP_SPECIFIC_PASSWORD" \
  --team-id "$TEAM_ID" \
  --wait

echo "[INF] Notarized successfully!"

# Stapling

echo "[INF] Stapling"

xcrun stapler staple -v "$APP_BUNDLE"
xcrun stapler validate "$APP_BUNDLE"

echo "[INF] Stapled successfully!"

# Cleanup

if [ "$CI" = true ]; then
  security delete-keychain "$KEYCHAIN"
fi

echo "[INF] --- [DONE] ---"
