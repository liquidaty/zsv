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

echo "[INF] CPPCHECK_PROJECT_FILE:    $CPPCHECK_PROJECT_FILE"
echo "[INF] CPPCHECK_BUILD_DIR:       $CPPCHECK_BUILD_DIR"
echo "[INF] CPPCHECK_XML_OUTPUT_FILE: $CPPCHECK_XML_OUTPUT_FILE"
echo "[INF] CPPCHECK_HTML_REPORT_DIR: $CPPCHECK_HTML_REPORT_DIR"

if [ "$(uname -s)" = "Darwin" ]; then
  alias nproc="sysctl -n hw.logicalcpu"
fi

mkdir -p "$CPPCHECK_BUILD_DIR"

echo "[INF] Generating XML report..."
cppcheck \
  -j "$(nproc)" \
  --quiet \
  --enable=all \
  --project="$CPPCHECK_PROJECT_FILE" \
  --xml \
  --output-file="$CPPCHECK_XML_OUTPUT_FILE"

ls -hl "$CPPCHECK_XML_OUTPUT_FILE"

echo "[INF] Generating HTML report..."
cppcheck-htmlreport \
  --title="zsv" \
  --file="$CPPCHECK_XML_OUTPUT_FILE" \
  --report-dir="$CPPCHECK_HTML_REPORT_DIR" \
  --source-dir="$PWD"

# GitHub Actions
if [ "$CI" = true ]; then
  CPPCHECK_XML_ARTIFACT_NAME="zsv-cppcheck-xml-report-$GITHUB_RUN_ID.zip"
  CPPCHECK_HTML_ARTIFACT_NAME="zsv-cppcheck-html-report-$GITHUB_RUN_ID.zip"

  echo "[INF] Generating ZIP archive (XML)... [$CPPCHECK_XML_ARTIFACT_NAME]"
  zip "$CPPCHECK_XML_ARTIFACT_NAME" "$CPPCHECK_XML_OUTPUT_FILE"

  echo "[INF] Generating ZIP archive (HTML)... [$CPPCHECK_HTML_ARTIFACT_NAME]"
  zip -r "$CPPCHECK_HTML_ARTIFACT_NAME" "$CPPCHECK_HTML_REPORT_DIR"

  {
    echo "CPPCHECK_XML_ARTIFACT_NAME=$CPPCHECK_XML_ARTIFACT_NAME"
    echo "CPPCHECK_HTML_ARTIFACT_NAME=$CPPCHECK_HTML_ARTIFACT_NAME"
  } >>"$GITHUB_ENV"

  echo "[INF] Generating Markdown step summary..."

  BRANCH=
  if [ "$GITHUB_REF_TYPE" = "branch" ]; then
    if [ "$GITHUB_EVENT_NAME" = "push" ]; then
      BRANCH="$GITHUB_REF_NAME"
    elif [ "$GITHUB_EVENT_NAME" = "pull_request" ]; then
      BRANCH="$GITHUB_HEAD_REF"
    fi
  elif [ "$GITHUB_REF_TYPE" = "tag" ]; then
    BRANCH="main"
  fi

  SOURCE_LINK="[{file}:{line}](https://github.com/liquidaty/zsv/blob/$BRANCH/{file}#L{line})"
  CWE_LINK="[{cwe}](https://cwe.mitre.org/data/definitions/{cwe}.html)"
  TEMPLATE="| $SOURCE_LINK | {column} | {severity} | {id} | {message} | $CWE_LINK |"
  {
    echo "<details>"
    echo "<summary>Cppcheck Static Analysis Summary</summary>"
    echo
    echo "| File:Line | Column | Severity |  ID   | Message |  CWE  |"
    echo "| :-------: | :----: | :------: | :---: | :-----: | :---: |"
    cppcheck \
      -j "$(nproc)" \
      --quiet \
      --enable=all \
      --project="$CPPCHECK_PROJECT_FILE" \
      --template="$TEMPLATE" \
      2>&1
    echo "</details>"
  } >>"$GITHUB_STEP_SUMMARY"
fi

echo "[INF] --- [DONE] ---"
