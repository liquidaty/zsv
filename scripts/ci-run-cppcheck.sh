#!/bin/sh

set -e

echo "[INF] Running $0"

if ! which cppcheck >/dev/null; then
  echo "[ERR] cppcheck is not installed!"
  exit 1
fi

VERSION=$(cppcheck --version | sed 's/^[^0-9]*//g' | sed 's/ .*$//g')
echo "[INF] cppcheck version [$VERSION]"

CPPCHECK_PROJECT_FILE=".cppcheck"
CPPCHECK_BUILD_DIR="cppcheck-build-dir"
CPPCHECK_XML_OUTPUT_FILE="cppcheck.xml"
CPPCHECK_HTML_REPORT_DIR="cppcheck-html-report-dir"

echo "[INF] CPPCHECK_PROJECT_FILE: $CPPCHECK_PROJECT_FILE"
echo "[INF] CPPCHECK_BUILD_DIR: $CPPCHECK_BUILD_DIR"
echo "[INF] CPPCHECK_XML_OUTPUT_FILE: $CPPCHECK_XML_OUTPUT_FILE"
echo "[INF] CPPCHECK_HTML_REPORT_DIR: $CPPCHECK_HTML_REPORT_DIR"

mkdir -p "$CPPCHECK_BUILD_DIR"

echo "[INF] Generating XML report..."
cppcheck \
  --quiet \
  --enable=all \
  --project="$CPPCHECK_PROJECT_FILE" \
  --xml 2>"$CPPCHECK_XML_OUTPUT_FILE"

ls -Gghl "$CPPCHECK_XML_OUTPUT_FILE"

echo "[INF] Generating HTML report..."
cppcheck-htmlreport \
  --title="zsv" \
  --file="$CPPCHECK_XML_OUTPUT_FILE" \
  --report-dir="$CPPCHECK_HTML_REPORT_DIR" \
  --source-dir="$PWD"

# GitHub Actions
if [ "$CI" = true ]; then
  CPPCHECK_XML_ARTIFACT_NAME="zsv-cppcheck-xml-report-$(date "+%s").zip"
  CPPCHECK_HTML_ARTIFACT_NAME="zsv-cppcheck-html-report-$(date "+%s").zip"

  echo "[INF] Generating ZIP archive (XML)... [$CPPCHECK_XML_ARTIFACT_NAME]"
  zip "$CPPCHECK_XML_ARTIFACT_NAME" "$CPPCHECK_XML_OUTPUT_FILE"

  echo "[INF] Generating ZIP archive (HTML)... [$CPPCHECK_HTML_ARTIFACT_NAME]"
  zip -r "$CPPCHECK_HTML_ARTIFACT_NAME" "$CPPCHECK_HTML_REPORT_DIR"

  {
    echo "CPPCHECK_XML_ARTIFACT_NAME=$CPPCHECK_XML_ARTIFACT_NAME"
    echo "CPPCHECK_HTML_ARTIFACT_NAME=$CPPCHECK_HTML_ARTIFACT_NAME"
  } >>"$GITHUB_ENV"
fi

echo "[INF] --- [DONE] ---"
