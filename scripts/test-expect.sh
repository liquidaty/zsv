#!/bin/sh -eu

SCRIPT_DIR=$(dirname "$0")

EXPECT_TIMEOUT=${EXPECT_TIMEOUT:-5}

TARGET="${1:-}"
STAGE="${2:-}"

if [ -n "$STAGE" ]; then
  STAGE="-$STAGE"
fi

export TARGET
export STAGE

export CAPTURED_OUTPUT="$TMP_DIR/$TARGET$STAGE.out"
export EXPECTED_OUTPUT="$EXPECTED_PATH/$TARGET$STAGE.out"

MATCHED=false

cleanup() {
  if $MATCHED; then
    if [ -z "$STAGE" ]; then
      tmux send-keys -t "$TARGET" "q"
    fi
    exit 0
  fi

  tmux send-keys -t "$TARGET" "q"
  echo 'Incorrect output:'
  cat "$CAPTURED_OUTPUT"
  echo 'Expected output:'
  cat "$EXPECTED_OUTPUT"
  echo "${CMP} $CAPTURED_OUTPUT $EXPECTED_OUTPUT"
  ${CMP} "$CAPTURED_OUTPUT" "$EXPECTED_OUTPUT"
  exit 1
}

trap cleanup INT TERM QUIT

printf "\n%s, %s" "$TARGET" "$STAGE" >>"$TIMINGS_CSV"

set +e
MATCHED_TIME=$(/usr/bin/time -p timeout -k $((EXPECT_TIMEOUT + 1)) "$EXPECT_TIMEOUT" "$SCRIPT_DIR"/test-retry-capture-cmp.sh 2>&1)
STATUS=$?
set -e

if [ $STATUS -eq 0 ]; then
  MATCHED_TIME=$(echo "$MATCHED_TIME" | grep '^real' | cut -d' ' -f2)
  if echo "$MATCHED_TIME" | grep -qE '^[0-9]*\.?[0-9]+$' >/dev/null 2>&1; then
    MATCHED=true
    echo "$TARGET$STAGE took $MATCHED_TIME"
    printf ", %s" "$MATCHED_TIME" >>"$TIMINGS_CSV"
  else
    echo "Invalid timing value! [$MATCHED_TIME]"
    printf ", error" >>"$TIMINGS_CSV"
  fi
fi

cleanup
