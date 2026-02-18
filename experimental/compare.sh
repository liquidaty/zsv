#!/bin/sh

# Note - assumes:
#   - zsv_main is the zsv executable compiled from the main branch
#   - worldcitiespop_cr.csv, worldcitiespop_crlf.csv, worldcitiespop_lf.csv have been prepared locally by running prepare_data.sh

# Run with:
#   sh compare.txt <num test runs>
# or (if wanting to redirect all output to a file):
#.   sh compare.sh <num test runs> > compare.txt 2>&1
# where <num test runs> is optional (default = 5)

# Function to run benchmark with decimal support
bench() {
    LABEL=$1
    COMMAND=$2
    RUNS=$3
    TOTAL=0

    echo "> $LABEL:"
    echo
    echo "Testing..."
    
    # Optional: Warm-up run (not counted in average)
    eval "$COMMAND" > /dev/null 2>&1

    for i in $(seq 1 $RUNS); do
        # Extract the 'real' time. 
        # We use /usr/bin/time -p to ensure a standard format: "real 0.12"
        # The 'eval' allows us to handle redirects like '<' and '>'
        TIME_TAKEN=$({ /usr/bin/time -p sh -c "$COMMAND" ; } 2>&1 | awk '/real/ {print $2}')
        
        # Add to total using 'bc' for floating point math
        TOTAL=$(echo "$TOTAL + $TIME_TAKEN" | bc)
    done

    # Calculate average to 3 decimal places
    AVG=$(echo "scale=3; $TOTAL / $RUNS" | bc)
    echo "  > Average over $RUNS run(s): ${AVG}s"
    echo ""
}

  echo "*********************"
  echo "*** Test - Start  ***"
  echo "*********************"

  RUNS=${1:-5} # Default test runs is 5

# compare count times and values
  echo
  echo "Running count tests with LF..." 
  echo
  bench "count-neon-columns-callback-v3 > LF" "./count-neon-columns-callback-v3 < worldcitiespop_lf.csv" "$RUNS"
  bench "zsv_main > LF" "zsv_main count < worldcitiespop_lf.csv" "$RUNS"
  bench "zsv > LF" "zsv count < worldcitiespop_lf.csv" "$RUNS"
  echo 
  echo "Running count tests with CRLF..." 
  echo
  bench "count-neon-columns-callback-v3 > CRLF" "./count-neon-columns-callback-v3 < worldcitiespop_crlf.csv" "$RUNS"
  bench "zsv_main > CRLF" "zsv_main count < worldcitiespop_crlf.csv" "$RUNS"
  bench "zsv > CRLF" "zsv count < worldcitiespop_crlf.csv" "$RUNS"
  echo
  echo "Running count tests with CR..." 
  echo
  bench "count-neon-columns-callback-v3 > CR" "./count-neon-columns-callback-v3 < worldcitiespop_cr.csv" "$RUNS"
  bench "zsv_main > CR" "zsv_main count < worldcitiespop_cr.csv" "$RUNS"
  bench "zsv > CR" "zsv count < worldcitiespop_cr.csv" "$RUNS"
# compare select times
  echo
  echo "Running select tests with LF..." 
  echo
  bench "count-neon-columns-callback-v3 > LF" "./count-neon-columns-callback-v3 test1 < worldcitiespop_lf.csv >/dev/null" "$RUNS"
  bench "zsv_main > LF" "zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_lf.csv >/dev/null" "$RUNS"
  bench "zsv_main > LF" "zsv select -n -- 1 2 3 6 7 < worldcitiespop_lf.csv >/dev/null" "$RUNS"
  echo
  echo "Running select tests with CRLF..." 
  echo
  echo  
  bench "count-neon-columns-callback-v3 > CRLF" "./count-neon-columns-callback-v3 test1 < worldcitiespop_crlf.csv >/dev/null" "$RUNS"
  bench "zsv_main > CRLF" "zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_crlf.csv >/dev/null" "$RUNS"
  bench "zsv > CRLF" "zsv select -n -- 1 2 3 6 7 < worldcitiespop_crlf.csv >/dev/null" "$RUNS"
  echo
  echo "Running select tests with CR..." 
  echo
  bench "count-neon-columns-callback-v3 > CR" "./count-neon-columns-callback-v3 test1 < worldcitiespop_cr.csv >/dev/null" "$RUNS"
  bench "zsv_main > CR" "zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_cr.csv >/dev/null" "$RUNS"
  bench "zsv > CR" "zsv select -n -- 1 2 3 6 7 < worldcitiespop_cr.csv >/dev/null" "$RUNS"

  echo "*******************"
  echo "*** Test - End  ***"
  echo "*******************"
