#!/bin/sh -eu

# Wait for a tmux session to end, i.e. for the program under test to have exited.
# Usage: test-wait-for-exit.sh <socket-and-session-name>
# Exits non-zero if the session is still alive after $EXPECT_TIMEOUT seconds.

TARGET="$1"
TIMEOUT="${EXPECT_TIMEOUT:-5}"

TRIES=$((TIMEOUT * 20)) # polled every 0.05s, as in test-retry-capture-cmp.sh
while [ "$TRIES" -gt 0 ]; do
  tmux -L "$TARGET" has-session -t "$TARGET" 2>/dev/null || exit 0
  sleep 0.05
  TRIES=$((TRIES - 1))
done

echo "$TARGET: still running after ${TIMEOUT}s"
exit 1
