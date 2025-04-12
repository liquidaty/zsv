#!/bin/sh -eu

SCRIPT_DIR=$(dirname "$0")

export TARGET="$1"
if [ -z "${2:-}" ]; then
  export STAGE=""
else
  export STAGE=-"$2"
fi

export CAPTURE="${TMP_DIR}/$TARGET$STAGE".out
EXPECTED="$EXPECTED_PATH/$TARGET$STAGE".out
export EXPECTED
MATCHED=false

EXPECT_TIMEOUT=${EXPECT_TIMEOUT:-5}

cleanup() {
  if $MATCHED; then
    if [ -z "$STAGE" ]; then
      tmux send-keys -t "$TARGET" "q"
    fi
    exit 0
  fi

  tmux send-keys -t "$TARGET" "q"
  echo 'Incorrect output:'
  cat "$CAPTURE"
  echo "${CMP} $CAPTURE $EXPECTED"
  ${CMP} "$CAPTURE" "$EXPECTED"
  exit 1
}

trap cleanup INT TERM QUIT

printf "\n%s, %s" "$TARGET" "${2:-}" >> "${TIMINGS_CSV}"

set +e
MATCH_TIME=$(time -p timeout -k $(( t + 1 )) "$EXPECT_TIMEOUT" "${SCRIPT_DIR}"/test-retry-capture-cmp.sh 2>&1)
STATUS=$?
set -e

if [ $STATUS -eq 0 ]; then
  MATCHED=true
  MATCH_TIME=$(echo "$MATCH_TIME" | head -n 1 | cut -f 2 -d ' ')
  echo "$TARGET$STAGE took $MATCH_TIME"
  printf ", %s" "$MATCH_TIME" >> "${TIMINGS_CSV}"
fi

cleanup
