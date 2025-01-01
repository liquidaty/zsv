#!/bin/sh -eu

script_dir=$(dirname "$0")

export TARGET="$1"
if [ -z "${2:-}" ]; then
  export STAGE=""
else
  export STAGE=-"$2"
fi
export CAPTURE="${TMP_DIR}/$TARGET$STAGE".out
EXPECTED="$EXPECTED_PATH/$TARGET$STAGE".out
export EXPECTED
matched=false

t=${EXPECT_TIMEOUT:-5}

cleanup() {
  if $matched; then
    if [ -z "$STAGE" ]; then
      tmux send-keys -t "$TARGET" "q"
    fi
    exit 0
  fi

  tmux send-keys -t "$TARGET" "q"
  echo 'Incorrect output:'
  cat "$CAPTURE"
  echo "${CMP} -s $CAPTURE $EXPECTED"
  ${CMP} -s "$CAPTURE" "$EXPECTED"
  exit 1
}

trap cleanup INT TERM QUIT

printf "\n%s, %s" "$TARGET" "${2:-}" >> "${TIMINGS_CSV}"

set +e
match_time=$(time -p timeout -k $(( t + 1 )) $t "${script_dir}"/test-retry-capture-cmp.sh 2>&1)
status=$?
set -e

if [ $status -eq 0 ]; then
  matched=true
  match_time=$(echo "$match_time" | head -n 1 | cut -f 2 -d ' ')
  echo "$TARGET$STAGE took $match_time"
  printf ", %s" "$match_time" >> "${TIMINGS_CSV}"
fi

cleanup
