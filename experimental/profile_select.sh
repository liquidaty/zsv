#!/bin/sh

set -eu

RUNS=100
INPUT_FILE="experimental/worldcitiespop_lf.csv"
BIN="zsv"
COLS="1 2 3 6 7"
OUT_DIR="experimental/profiles"
DO_PROFILE=1
SAMPLE_USE_SUDO=0

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  -r, --runs N        Number of timing runs (default: ${RUNS})
  -f, --file PATH     Input CSV file (default: ${INPUT_FILE})
  -b, --bin PATH      zsv binary/command (default: ${BIN})
  -c, --cols "LIST"   select columns (default: "${COLS}")
  -o, --out DIR       Output directory (default: ${OUT_DIR})
      --no-profile    Skip xctrace profiling and run timings only
      --sample-sudo   Use sudo for sample fallback when xctrace is unavailable
  -h, --help          Show this help
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
  -r | --runs)
    RUNS="$2"
    shift 2
    ;;
  -f | --file)
    INPUT_FILE="$2"
    shift 2
    ;;
  -b | --bin)
    BIN="$2"
    shift 2
    ;;
  -c | --cols)
    COLS="$2"
    shift 2
    ;;
  -o | --out)
    OUT_DIR="$2"
    shift 2
    ;;
  --no-profile)
    DO_PROFILE=0
    shift
    ;;
  --sample-sudo)
    SAMPLE_USE_SUDO=1
    shift
    ;;
  -h | --help)
    usage
    exit 0
    ;;
  *)
    echo "Unknown option: $1" >&2
    usage >&2
    exit 1
    ;;
  esac
done

if [ ! -f "$INPUT_FILE" ]; then
  echo "Input file not found: $INPUT_FILE" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
TS="$(date +%Y%m%d-%H%M%S)"
TIMINGS_FILE="${OUT_DIR}/select-timings-${TS}.txt"
SUMMARY_FILE="${OUT_DIR}/select-summary-${TS}.txt"
TRACE_FILE="${OUT_DIR}/select-timeprofiler-${TS}.trace"
TRACE_EXPORT="${OUT_DIR}/select-timeprofiler-${TS}.xml"
SAMPLE_FILE="${OUT_DIR}/select-sample-${TS}.txt"

SELECT_CMD="${BIN} select -n -- ${COLS} < \"${INPUT_FILE}\""

echo "Select command:" | tee -a "$SUMMARY_FILE"
echo "  ${SELECT_CMD}" | tee -a "$SUMMARY_FILE"
echo | tee -a "$SUMMARY_FILE"

echo "Warming up..."
sh -c "${SELECT_CMD} > /dev/null"

echo "Running ${RUNS} timed runs..."
: > "$TIMINGS_FILE"
i=1
while [ "$i" -le "$RUNS" ]; do
  t=$({ /usr/bin/time -p sh -c "${SELECT_CMD} > /dev/null"; } 2>&1 | awk '/^real / {print $2}')
  echo "$t" >> "$TIMINGS_FILE"
  i=$((i + 1))
done

awk '
  BEGIN { min=-1; max=0; sum=0; n=0 }
  {
    x=$1+0;
    if(min < 0 || x < min) min=x;
    if(x > max) max=x;
    sum+=x;
    vals[n]=x;
    n++;
  }
  END {
    if(n == 0) {
      print "No timings recorded";
      exit 1;
    }
    mean = sum / n;
    ssd = 0;
    for(i=0;i<n;i++) {
      d = vals[i] - mean;
      ssd += d*d;
    }
    sd = (n > 1) ? sqrt(ssd / (n - 1)) : 0;
    printf("runs: %d\nmean_s: %.6f\nstddev_s: %.6f\nmin_s: %.6f\nmax_s: %.6f\n", n, mean, sd, min, max);
  }
' "$TIMINGS_FILE" | tee -a "$SUMMARY_FILE"

if [ "$DO_PROFILE" -eq 1 ]; then
  if command -v xcrun >/dev/null 2>&1 && xcrun xctrace list templates 2>/dev/null | grep -q "Time Profiler"; then
    echo | tee -a "$SUMMARY_FILE"
    echo "Recording Time Profiler trace..." | tee -a "$SUMMARY_FILE"
    xcrun xctrace record \
      --template "Time Profiler" \
      --output "$TRACE_FILE" \
      --launch -- sh -c "${SELECT_CMD} > /dev/null"

    if xcrun xctrace export --input "$TRACE_FILE" --output "$TRACE_EXPORT" >/dev/null 2>&1; then
      echo "trace: ${TRACE_FILE}" | tee -a "$SUMMARY_FILE"
      echo "trace_export: ${TRACE_EXPORT}" | tee -a "$SUMMARY_FILE"
    else
      echo "trace: ${TRACE_FILE}" | tee -a "$SUMMARY_FILE"
      echo "trace export failed; open trace directly in Instruments" | tee -a "$SUMMARY_FILE"
    fi
  else
    echo | tee -a "$SUMMARY_FILE"
    echo "xctrace not available; falling back to sample(1)" | tee -a "$SUMMARY_FILE"
    # profile the actual zsv process directly
    set -- $COLS
    "$BIN" select -n "$INPUT_FILE" -- "$@" > /dev/null &
    PROFILE_PID=$!
    # profile for a short, fixed window to capture hotspots
    SAMPLE_ERR_FILE="${OUT_DIR}/select-sample-${TS}.err"
    SAMPLE_CMD="sample \"$PROFILE_PID\" 5 -mayDie -file \"$SAMPLE_FILE\""
    if [ "$SAMPLE_USE_SUDO" -eq 1 ]; then
      SAMPLE_CMD="sudo ${SAMPLE_CMD}"
    fi

    if sh -c "$SAMPLE_CMD" > /dev/null 2> "$SAMPLE_ERR_FILE"; then
      echo "sample_profile: ${SAMPLE_FILE}" | tee -a "$SUMMARY_FILE"
    else
      echo "sample fallback failed; no profiler output captured" | tee -a "$SUMMARY_FILE"
      if [ -s "$SAMPLE_ERR_FILE" ]; then
        echo "sample_error: ${SAMPLE_ERR_FILE}" | tee -a "$SUMMARY_FILE"
        if [ "$SAMPLE_USE_SUDO" -eq 0 ] && grep -qi "try running with \`sudo\`" "$SAMPLE_ERR_FILE"; then
          echo "hint: rerun with --sample-sudo to allow sample to attach" | tee -a "$SUMMARY_FILE"
        fi
      fi
    fi
    wait "$PROFILE_PID" || true
  fi
fi

echo | tee -a "$SUMMARY_FILE"
echo "timings: ${TIMINGS_FILE}" | tee -a "$SUMMARY_FILE"
echo "summary: ${SUMMARY_FILE}" | tee -a "$SUMMARY_FILE"
