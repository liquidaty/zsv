#!/bin/sh -eu

while true; do
  tmux capture-pane -t "$TARGET" -p >"$CAPTURE"

  if ${CMP} -s "$CAPTURE" "$EXPECTED"; then
    exit 0
  fi

  sleep 0.025
done
