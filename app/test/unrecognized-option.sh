#!/bin/sh
# Regression test for uniform rejection of unrecognized command-line options.
#
# Contract (see zsv_arg_is_option / zsv_err_unrecognized_option in app/utils/arg.c):
# for a token that is option-shaped ('-x', '-xyz', '--name') but not defined by the
# command, every command must
#   1. exit nonzero with the same code (1) across all commands,
#   2. print a stderr line naming the token as an unrecognized option, and
#   3. write nothing to stdout,
# identically for the short (-Z), long (--bogus) and attached (-Zfoo) forms.
#
# Usage: unrecognized-option.sh <bin_dir> <exe_suffix> <tmp_dir>
set -u

BIN="$1"   # directory holding the zsv_<cmd> standalone binaries
EXE="$2"   # executable suffix ("" on unix, ".exe" on windows)
TMP="$3"   # scratch directory

DATA="$TMP/unrecognized-option.csv"
printf 'a,b,c\n1,2,3\n' > "$DATA"

# Offenders that were fixed, plus the already-compliant regression guards. Each
# entry is verified only if its standalone binary exists (some are config-gated).
CMDS="count select sql stack jq desc serialize flatten 2tsv 2json 2db compare echo pretty 2toon overwrite paste check prop rm mv"

rc=0
for cmd in $CMDS; do
  exe="$BIN/zsv_$cmd$EXE"
  [ -x "$exe" ] || continue
  for flag in "-Z" "--bogus" "-Zfoo"; do
    # A few commands take a mandatory positional before options.
    case "$cmd" in
      jq)      "$exe" '.' "$flag" "$DATA" </dev/null >"$TMP/uo.out" 2>"$TMP/uo.err"; code=$? ;;
      compare) "$exe" "$flag" "$DATA" "$DATA" </dev/null >"$TMP/uo.out" 2>"$TMP/uo.err"; code=$? ;;
      *)       "$exe" "$flag" "$DATA" </dev/null >"$TMP/uo.out" 2>"$TMP/uo.err"; code=$? ;;
    esac
    if [ "$code" != 1 ]; then
      echo "  FAIL: $cmd $flag -> exit $code (want 1)"; rc=1
    fi
    if ! grep -iq "unrecognized.*$flag" "$TMP/uo.err"; then
      echo "  FAIL: $cmd $flag -> stderr does not name it unrecognized"; rc=1
    fi
    if [ -s "$TMP/uo.out" ]; then
      echo "  FAIL: $cmd $flag -> wrote to stdout"; rc=1
    fi
  done
done

# --- Bare '-' is the stdin sentinel, never an unrecognized option ---
# For commands that read a single CSV stream, `cmd -` must read stdin: exit 0,
# produce output, and print no "unrecognized" diagnostic.
JSON='{"a":1}'
CSV='a,b,c
1,2,3'
for cmd in count select desc serialize flatten 2tsv 2json 2toon pretty echo check sql count-pull select-pull jq; do
  exe="$BIN/zsv_$cmd$EXE"
  [ -x "$exe" ] || continue
  case "$cmd" in
    jq)  printf '%s\n' "$JSON" | "$exe" '.' - >"$TMP/uo.out" 2>"$TMP/uo.err"; code=$? ;;
    sql) printf '%s\n' "$CSV" | "$exe" 'select * from data' - >"$TMP/uo.out" 2>"$TMP/uo.err"; code=$? ;;
    *)   printf '%s\n' "$CSV"  | "$exe" -    >"$TMP/uo.out" 2>"$TMP/uo.err"; code=$? ;;
  esac
  if [ "$code" != 0 ]; then
    echo "  FAIL: $cmd - (bare dash) -> exit $code (want 0, should read stdin)"; rc=1
  fi
  if grep -iq "unrecognized" "$TMP/uo.err"; then
    echo "  FAIL: $cmd - (bare dash) -> misreported as unrecognized"; rc=1
  fi
  # 'check' is a validator: no stdout on clean input. Others must echo stdin through.
  if [ "$cmd" != check ] && [ ! -s "$TMP/uo.out" ]; then
    echo "  FAIL: $cmd - (bare dash) -> produced no output from stdin"; rc=1
  fi
done

# For commands that cannot read stdin (they rewind/register inputs by path),
# a bare '-' must still not be misreported as an unrecognized option.
for cmd in stack 2db compare paste; do
  exe="$BIN/zsv_$cmd$EXE"
  [ -x "$exe" ] || continue
  case "$cmd" in
    compare) printf '%s\n' "$CSV" | "$exe" - - >"$TMP/uo.out" 2>"$TMP/uo.err" ;;
    *)       printf '%s\n' "$CSV" | "$exe" -   >"$TMP/uo.out" 2>"$TMP/uo.err" ;;
  esac
  if grep -iq "unrecognized" "$TMP/uo.err"; then
    echo "  FAIL: $cmd - (bare dash) -> misreported as unrecognized"; rc=1
  fi
done

exit $rc
