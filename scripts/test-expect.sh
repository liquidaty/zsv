#!/bin/sh -eu

# The Makefile target which will be the test name
TARGET="$1"

# The intermediate test stage if the test is split into multiple stages
# if it is blank then it is the last stage
if [ -z "${2:-}" ]; then
   STAGE=""
else
   STAGE=-"$2"
fi

# The capture file that is written to if we fail to match. If the capture is correct
# but expected is missing or wrong, then we can copy this to the expected location
CAPTURE="${TMP_DIR}/$TARGET$STAGE".out
# The expected output to match against
EXPECTED="$EXPECTED_PATH/$TARGET$STAGE".out

# write Git hash, target, stage, date, time to the CSV timings file
printf "\n%s, %s, %s, %s" "$(git rev-parse --short HEAD)" "$TARGET" "${2:-}" "$(date '+%F, %T')" >> "${TIMINGS_CSV}"

# temporarily disable error checking
set +e
# run the expect utility which will repeatedly
# try to match the expected output to the screen captured by tmux. If successfull
# it will output the elapsed time to stdout and therefor match_time, on failure it
# will print to stderr which the user will see
match_time=$($EXPECT_BIN "$EXPECTED" "$TARGET" "$CAPTURE" "$EXPECT_TIMEOUT")
status=$?
set -e

if [ $status -eq 0 ]; then
  # write the time it took to match the expected text with the capture
  echo "$TARGET$STAGE took $match_time"
  printf ", %s" "$match_time" >> "${TIMINGS_CSV}"
else
  echo "$TARGET$STAGE did not match"
fi

# Quit if this is the last stage or there was an error
if [ -z "$STAGE" ] || [ $status -eq 1 ]; then
  tmux send-keys -t "$TARGET" "q"
fi

exit $status
