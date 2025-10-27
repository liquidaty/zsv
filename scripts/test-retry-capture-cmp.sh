#!/bin/sh -eu

while :; do
  tmux capture-pane -t "$TARGET" -p >"$CAPTURED_OUTPUT"

  if ${CMP} -s "$CAPTURED_OUTPUT" "$EXPECTED_OUTPUT"; then
    exit 0
  fi

  sleep 0.025
done
