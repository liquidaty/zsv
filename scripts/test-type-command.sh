#!/bin/sh -eu

# Type a command at the shell prompt ("$ ") of a tmux session and press Enter.
# Usage: test-type-command.sh <socket-and-session-name> <command>
# Keys typed before the prompt appears are echoed ahead of it, so typing waits for the prompt.
# A shell whose line editor queries the terminal after printing its prompt (e.g. BusyBox
# ash) drops keys that arrive during the query, so the command is typed without Enter,
# checked on screen, and typed again after <ctrl>u if any of it is missing.

TARGET="$1"
COMMAND="$2"

ATTEMPTS=5
CHECKS=20 # polled every 0.1s

last_line() {
  tmux -L "$TARGET" capture-pane -p -t "$TARGET" | grep -v '^$' | tail -1
}

# the prompt with nothing typed after it (capture-pane drops the trailing space)
wait_for_prompt() {
  TRIES=$CHECKS
  while [ "$TRIES" -gt 0 ] && [ "$(last_line)" != '$' ]; do
    sleep 0.1
    TRIES=$((TRIES - 1))
  done
}

while [ "$ATTEMPTS" -gt 0 ]; do
  wait_for_prompt
  tmux -L "$TARGET" send-keys -t "$TARGET" -l "$COMMAND"
  TRIES=$CHECKS
  while [ "$TRIES" -gt 0 ]; do
    if [ "$(last_line)" = "\$ $COMMAND" ]; then
      tmux -L "$TARGET" send-keys -t "$TARGET" Enter
      exit 0
    fi
    sleep 0.1
    TRIES=$((TRIES - 1))
  done
  tmux -L "$TARGET" send-keys -t "$TARGET" C-u
  ATTEMPTS=$((ATTEMPTS - 1))
done

echo "$TARGET: unable to type: $COMMAND"
exit 1
