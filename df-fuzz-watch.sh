#!/usr/bin/env bash
#
# df-fuzz-watch.sh
#
# 1. Run `make df-clean-all` then `make df-run ZSV_BRANCH=<branch>`, recording the start time.
# 2. Every two minutes, run `make df-list-crashes` and record the current time.
#    - If any crashes are listed: run `make df-stop`, report the crash(es) and the
#      total elapsed time, then exit.
#    - Otherwise: report that no crash was found and the total elapsed time, then keep watching.
#
# Usage:
#   ./df-fuzz-watch.sh                 # uses defaults below
#   ZSV_BRANCH=some-branch ./df-fuzz-watch.sh
#   POLL_SECONDS=120 MAX_MINUTES=0 ./df-fuzz-watch.sh
#
# Environment overrides:
#   ZSV_BRANCH    git branch to fuzz                       (default: main)
#   JOBS          parallel fuzzing jobs passed to df-run   (default: 2, the Makefile default)
#   POLL_SECONDS  seconds between crash checks             (default: 120 = two minutes)
#   MAX_MINUTES   safety cap; stop watching after this     (default: 0 = run until a crash is found)

set -u

# --- Configuration -----------------------------------------------------------
ZSV_BRANCH="${ZSV_BRANCH:-main}"
POLL_SECONDS="${POLL_SECONDS:-120}"
MAX_MINUTES="${MAX_MINUTES:-0}"

# Run from the directory this script lives in (the zsv fuzz/ dir) so `make` finds the Makefile.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || { echo "ERROR: cannot cd into $SCRIPT_DIR" >&2; exit 1; }

LOG_FILE="$SCRIPT_DIR/df-fuzz-watch.log"

# --- Helpers -----------------------------------------------------------------
# Echo to stdout and append to the log file with a timestamp prefix.
log() {
  local ts
  ts="$(date '+%Y-%m-%d %H:%M:%S %Z')"
  printf '[%s] %s\n' "$ts" "$*" | tee -a "$LOG_FILE"
}

# Human-readable HH:MM:SS from a number of seconds.
fmt_elapsed() {
  local total="$1"
  printf '%02d:%02d:%02d' $(( total / 3600 )) $(( (total % 3600) / 60 )) $(( total % 60 ))
}

JOBS_ARG=""
if [ -n "${JOBS:-}" ]; then
  JOBS_ARG="JOBS=$JOBS"
fi

# --- Step 1: clean, build/run, and record the start time ---------------------
log "=== df-fuzz-watch starting (branch=$ZSV_BRANCH, poll=${POLL_SECONDS}s, max=${MAX_MINUTES}m) ==="

log "Running: make df-clean-all ZSV_BRANCH=$ZSV_BRANCH"
if ! make df-clean-all ZSV_BRANCH="$ZSV_BRANCH" 2>&1 | tee -a "$LOG_FILE"; then
  log "ERROR: 'make df-clean-all' failed. Aborting."
  exit 1
fi

log "Running: make df-run ZSV_BRANCH=$ZSV_BRANCH $JOBS_ARG"
if ! make df-run ZSV_BRANCH="$ZSV_BRANCH" $JOBS_ARG 2>&1 | tee -a "$LOG_FILE"; then
  log "ERROR: 'make df-run' failed. Aborting."
  exit 1
fi

# Fuzzing has now started; this is t=0 for elapsed-time reporting.
START_EPOCH="$(date +%s)"
log "Fuzzing started. Start time recorded: $(date '+%Y-%m-%d %H:%M:%S %Z')"

# --- Step 2: poll for crashes every POLL_SECONDS -----------------------------
while true; do
  sleep "$POLL_SECONDS"

  NOW_EPOCH="$(date +%s)"
  ELAPSED=$(( NOW_EPOCH - START_EPOCH ))
  ELAPSED_HMS="$(fmt_elapsed "$ELAPSED")"

  log "--- Checking for crashes (current time: $(date '+%Y-%m-%d %H:%M:%S %Z'), elapsed: $ELAPSED_HMS) ---"

  # `make df-list-crashes` prints one line per crash file (empty if none).
  CRASHES="$(make df-list-crashes ZSV_BRANCH="$ZSV_BRANCH" 2>>"$LOG_FILE")"

  if [ -n "$CRASHES" ]; then
    CRASH_COUNT="$(printf '%s\n' "$CRASHES" | grep -c .)"
    log "CRASH(ES) FOUND ($CRASH_COUNT). Stopping fuzzer..."

    make df-stop ZSV_BRANCH="$ZSV_BRANCH" 2>&1 | tee -a "$LOG_FILE"

    log "================ RESULT ================"
    log "Crash(es) detected after $ELAPSED_HMS of fuzzing (branch: $ZSV_BRANCH)."
    log "Crash file(s):"
    printf '%s\n' "$CRASHES" | tee -a "$LOG_FILE"
    log "Total elapsed time: $ELAPSED_HMS"
    log "======================================="
    log "Tip: reproduce with 'make df-repro-crash ZSV_BRANCH=$ZSV_BRANCH CRASH_FILE=<file>'"
    exit 0
  fi

  log "No crash found yet. Total elapsed time: $ELAPSED_HMS"

  # Optional safety cap.
  if [ "$MAX_MINUTES" -gt 0 ] && [ "$ELAPSED" -ge $(( MAX_MINUTES * 60 )) ]; then
    log "Reached MAX_MINUTES=$MAX_MINUTES without a crash. Stopping fuzzer..."
    make df-stop ZSV_BRANCH="$ZSV_BRANCH" 2>&1 | tee -a "$LOG_FILE"
    log "================ RESULT ================"
    log "No crash found within $ELAPSED_HMS (branch: $ZSV_BRANCH)."
    log "Total elapsed time: $ELAPSED_HMS"
    log "======================================="
    exit 0
  fi
done
