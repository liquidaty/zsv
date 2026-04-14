#!/bin/sh -eu

# Use per-test tmux socket if TMUX_CMD is set, otherwise default
TMUX_CMD="${TMUX_CMD:-tmux}"

while :; do
  $TMUX_CMD capture-pane -t "$TARGET" -p >"$CAPTURED_OUTPUT"

  if ${CMP} -s "$CAPTURED_OUTPUT" "$EXPECTED_OUTPUT"; then
    exit 0
  fi

  sleep 0.025
done
