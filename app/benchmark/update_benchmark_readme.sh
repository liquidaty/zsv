#!/usr/bin/env bash
#
# Update benchmark README with links to latest results (one per OS+arch),
# git-add those files, and git-rm --cached any stale tracked results.
#
# Usage: update_benchmark_readme.sh <results_dir> <readme_file>

set -euo pipefail

RESULTS_DIR="${1:?Usage: update_benchmark_readme.sh <results_dir> <readme_file>}"
README="${2:?Usage: update_benchmark_readme.sh <results_dir> <readme_file>}"

TMPFILE=$(mktemp)
trap 'rm -f "$TMPFILE" "$README.tmp"' EXIT

# 1. Find the latest result file per OS-arch (newest by mtime first).
seen=""
: > "$TMPFILE"  # platform<TAB>filename, one per line

for f in $(ls -t "$RESULTS_DIR/" 2>/dev/null); do
  plat=""
  case "$f" in
    *-darwin-arm64-*|*_darwin_arm64_*)       plat="darwin-arm64" ;;
    *-darwin-x86_64-*|*_darwin_x86_64_*)     plat="darwin-x86_64" ;;
    *-linux-x86_64-*|*_linux_x86_64_*)       plat="linux-x86_64" ;;
    *-linux-arm64-*|*-linux-aarch64-*|*_linux_arm64_*|*_linux_aarch64_*) plat="linux-arm64" ;;
  esac
  [ -z "$plat" ] && continue
  case " $seen " in *" $plat "*) continue ;; esac
  seen="$seen $plat"
  printf '%s\t%s\n' "$plat" "$f" >> "$TMPFILE"
done

if [ ! -s "$TMPFILE" ]; then
  echo "No benchmark result files found in $RESULTS_DIR" >&2
  exit 1
fi

# 2. Build the links block as a temp file, then splice into README
MARKER_START="<!-- benchmark-results-start -->"
MARKER_END="<!-- benchmark-results-end -->"

{
  echo "$MARKER_START"
  sort "$TMPFILE" | while IFS=$'\t' read -r plat f; do
    echo "- [$plat: $f](results/$f)"
  done
  echo "$MARKER_END"
} > "$TMPFILE.block"

# 3. Update README: replace between markers, or insert after "Benchmark result summaries"
if grep -q "$MARKER_START" "$README"; then
  awk '
    /<!-- benchmark-results-start -->/ { skip=1; while ((getline line < "'"$TMPFILE.block"'") > 0) print line; next }
    /<!-- benchmark-results-end -->/   { skip=0; next }
    !skip { print }
  ' "$README" > "$README.tmp"
else
  awk '
    { print }
    /^#+ Benchmark result summaries/ {
      print ""
      while ((getline line < "'"$TMPFILE.block"'") > 0) print line
    }
  ' "$README" > "$README.tmp"
fi
mv "$README.tmp" "$README"
rm -f "$TMPFILE.block"

# 4. git-add the latest result files
cut -f2 "$TMPFILE" | while read -r f; do
  git add "$RESULTS_DIR/$f"
done

# 5. git-rm --cached any other tracked result files not in the latest set
for tracked in $(git ls-files "$RESULTS_DIR/"); do
  base=$(basename "$tracked")
  if ! cut -f2 "$TMPFILE" | grep -qxF "$base"; then
    git rm --cached "$RESULTS_DIR/$base" 2>/dev/null || true
  fi
done

echo "README.md updated and benchmark results staged."
echo "Latest results per platform:"
sort "$TMPFILE" | while IFS=$'\t' read -r plat f; do
  echo "  $plat: $f"
done
