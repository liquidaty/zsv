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
VERSION="$(date "+%d.%m.%y").$(date "+%s")"
if [ "$TAG" != "" ]; then
  VERSION="$("$PREFIX/bin/zsv" version | cut -d ' ' -f3 | cut -c2-)"
fi

RPM_DIR="$HOME/rpmbuild"
RPM_PKG="$PREFIX.rpm"
RPM_SPEC='zsv.spec'
RPM_SPEC_PATH="$RPM_DIR/SPECS/$RPM_SPEC"

echo "[INF] Creating rpm package [$RPM_PKG]"

echo "[INF] PWD:          $PWD"
echo "[INF] ARTIFACT_DIR: $ARTIFACT_DIR"
echo "[INF] PREFIX:       $PREFIX"
echo "[INF] ARCH:         $ARCH"
echo "[INF] VERSION:      $VERSION"

echo "[INF] Set up RPM build tree [$RPM_DIR]"
rm -rf "$RPM_DIR"
mkdir -p "$RPM_DIR/BUILD/usr" "$RPM_DIR/SPECS"

echo "[INF] Copying build artifacts"
cp -rfa "$PREFIX/bin" "$PREFIX/include" "$PREFIX/lib" "$RPM_DIR/BUILD/usr/"

echo "[INF] Generating spec file [$RPM_SPEC_PATH]"
cat << EOF > "$RPM_SPEC_PATH"
%define _build_id_links none
%define _rpmfilename $RPM_PKG

Name: zsv
Version: $VERSION
Release: 1%{?dist}
Summary: zsv+lib: world's fastest CSV parser, with an extensible CLI
License: MIT
URL: https://github.com/liquidaty/zsv
Vendor: Liquidaty <liquidaty@users.noreply.github.com>

%description
zsv+lib: world's fastest CSV parser, with an extensible CLI

%install
rm -rf %{buildroot}
cp -rfa %{_builddir} %{buildroot}
tree %{buildroot}

%clean
rm -rf %{buildroot}

%files
/usr/bin/zsv
/usr/lib/libzsv.a
/usr/include/zsv.h
/usr/include/zsv/*
EOF

echo "[INF] Dumping [$RPM_SPEC_PATH]"
echo "[INF] --- [$RPM_SPEC_PATH] ---"
cat -n "$RPM_SPEC_PATH"
echo "[INF] --- [$RPM_SPEC_PATH] ---"

tree "$RPM_DIR"

echo "[INF] Building"
rpmbuild -v --clean -bb "$RPM_SPEC_PATH"

mv "$RPM_DIR/RPMS/$RPM_PKG" "$ARTIFACT_DIR/"
rm -rf "$RPM_DIR"

ls -Gghl "$ARTIFACT_DIR/$RPM_PKG"

echo "[INF] --- [DONE] ---"
