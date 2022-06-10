#!/bin/sh

set -e

echo "[INF] Running $0"

if [ "$PREFIX" = "" ] || [ "$ARTIFACT_DIR" = "" ]; then
  echo "[ERR] One or more environment variable(s) are not set!"
  echo "[ERR] Set PREFIX and ARTIFACT_DIR before running $0 script."
  exit 1
fi

if [ ! -d "$PREFIX" ]; then
  echo "[ERR] PREFIX directory not found! [$PREFIX]"
  echo "[ERR] First build with PREFIX [$PREFIX] and then run $0."
  exit 1
fi

if [ ! -d "$ARTIFACT_DIR" ]; then
  echo "[ERR] Artifact directory not found! [$ARTIFACT_DIR]"
  exit 1
fi

ARCH="$(echo "$PREFIX" | cut -d '-' -f1)"
VERSION="$(date "+%-d.%-m.%y").$(date "+%s")"
if [ "$TAG" != "" ]; then
  VERSION="$("$PREFIX/bin/zsv" version | cut -d ' ' -f3 | cut -c2-)"
fi

NUGET_PKG="zsv.$VERSION.nupkg"
NUGET_SPEC='zsv.nuspec'
NUGET_SPEC_PATH="$PREFIX/$NUGET_SPEC"

echo "[INF] Creating nuget package [$NUGET_PKG]"

echo "[INF] PWD:          $PWD"
echo "[INF] PREFIX:       $PREFIX"
echo "[INF] ARTIFACT_DIR: $ARTIFACT_DIR"
echo "[INF] ARCH:         $ARCH"
echo "[INF] VERSION:      $VERSION"

echo "[INF] Creating spec file [$NUGET_SPEC_PATH]"
cat << EOF > "$NUGET_SPEC_PATH"
<?xml version="1.0"?>
<package>
  <metadata>
    <id>zsv</id>
    <version>$VERSION</version>
    <description>zsv+lib: world's fastest CSV parser, with an extensible CLI</description>
    <authors>liquidaty</authors>
    <projectUrl>https://github.com/liquidaty/zsv</projectUrl>
    <license type="expression">MIT</license>
    <requireLicenseAcceptance>false</requireLicenseAcceptance>
    <tags>native</tags>
  </metadata>

  <files>
    <file src="bin/*" target="native/bin" />
    <file src="lib/*" target="native/lib" />
    <file src="include/**/*.*" target="native/include" />
  </files>
</package>
EOF

echo "[INF] Dumping [$NUGET_SPEC_PATH]"
echo "[INF] --- [$NUGET_SPEC_PATH] ---"
cat -n "$NUGET_SPEC_PATH"
echo "[INF] --- [$NUGET_SPEC_PATH] ---"

tree "$PREFIX"

echo "[INF] Building [$NUGET_PKG]"
nuget pack "$NUGET_SPEC_PATH"
rm -f "$NUGET_SPEC"

echo "[INF] Renaming [$NUGET_PKG => zsv-$PREFIX.nupkg]"
mv "$NUGET_PKG" "$ARTIFACT_DIR/zsv-$PREFIX.nupkg"

echo "[INF] --- [DONE] ---"
