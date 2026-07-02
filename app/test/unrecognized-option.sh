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

exit $rc
