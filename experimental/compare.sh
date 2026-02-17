#!/bin/sh

# Note - assumes:
#   - zsv_main is the zsv executable compiled from the main branch
#   - worldcitiespop_cr.csv, worldcitiespop_crlf.csv, worldcitiespop_lf.csv have been prepared locally by running prepare_data.sh

# Run with:
#   sh compare.txt
# or (if wanting to redirect all output to a file):
#.   sh compare.sh > compare.txt 2>&1

# Function to run benchmark with decimal support
bench() {
    COMMAND=$1
    LABEL=$2
    REPS=100
    TOTAL=0

    echo "Testing $LABEL..."
    
    # Optional: Warm-up run (not counted in average)
    eval "$COMMAND" > /dev/null 2>&1

    for i in $(seq 1 $REPS); do
        # Extract the 'real' time. 
        # We use /usr/bin/time -p to ensure a standard format: "real 0.12"
        # The 'eval' allows us to handle redirects like '<' and '>'
        TIME_TAKEN=$({ /usr/bin/time -p sh -c "$COMMAND" ; } 2>&1 | awk '/real/ {print $2}')
        
        # Add to total using 'bc' for floating point math
        TOTAL=$(echo "$TOTAL + $TIME_TAKEN" | bc)
    done

    # Calculate average to 3 decimal places
    AVG=$(echo "scale=3; $TOTAL / $REPS" | bc)
    echo "  > Average over $REPS runs: ${AVG}s"
    echo ""
}

  echo "*********************"
  echo "*** Test - Start  ***"
  echo "*********************"

# compare count times and values
  echo
  echo "Running count tests with LF..." 
  echo
  echo "> count-neon-columns-callback-v3 > LF:" 
  #time ./count-neon-columns-callback-v3 < worldcitiespop_lf.csv
  bench "./count-neon-columns-callback-v3 < worldcitiespop_lf.csv"  
  echo "> zsv_main > LF:" 
  bench "zsv_main count < worldcitiespop_lf.csv"
  echo "> zsv > LF:" 
  bench "zsv count < worldcitiespop_lf.csv"
  echo 
  echo "Running count tests with CRLF..." 
  echo
  echo "> count-neon-columns-callback-v3 > CRLF:" 
  bench "./count-neon-columns-callback-v3 < worldcitiespop_crlf.csv"
  echo "> zsv_main > CRLF:" 
  bench "zsv_main count < worldcitiespop_crlf.csv"
  echo "> zsv > CRLF:" 
  bench "zsv count < worldcitiespop_crlf.csv"
  echo
  echo "Running count tests with CR..." 
  echo
  echo "> count-neon-columns-callback-v3 > CR:" 
  bench "./count-neon-columns-callback-v3 < worldcitiespop_cr.csv"
  echo "> zsv_main > CR:"   
  bench "zsv_main count < worldcitiespop_cr.csv"
  echo "> zsv > CR:"   
  bench "zsv count < worldcitiespop_cr.csv"
# # compare select times
  echo
  echo "Running select tests with LF..." 
  echo
  echo "> count-neon-columns-callback-v3 > LF:" 
  bench "./count-neon-columns-callback-v3 test1 < worldcitiespop_lf.csv >/dev/null"
  echo "> zsv_main > LF:" 
  bench "zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_lf.csv >/dev/null"
  echo "> zsv > LF:" 
  bench "zsv select -n -- 1 2 3 6 7 < worldcitiespop_lf.csv >/dev/null"
  echo
  echo "Running select tests with CRLF..." 
  echo
  echo "> count-neon-columns-callback-v3 > CRLF:" 
  bench "./count-neon-columns-callback-v3 test1 < worldcitiespop_crlf.csv >/dev/null"
  echo "> zsv_main > CRLF:" 
  bench "zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_crlf.csv >/dev/null"
  echo "> zsv > CRLF:" 
  bench "zsv select -n -- 1 2 3 6 7 < worldcitiespop_crlf.csv >/dev/null"
  echo
  echo "Running select tests with CR..." 
  echo
  echo "> count-neon-columns-callback-v3 > CR:" 
  bench "./count-neon-columns-callback-v3 test1 < worldcitiespop_cr.csv >/dev/null"
  echo "> zsv_main > CR:" 
  bench "zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_cr.csv >/dev/null"
  echo "> zsv > CR:" 
  bench "zsv select -n -- 1 2 3 6 7 < worldcitiespop_cr.csv >/dev/null"

  echo "*******************"
  echo "*** Test - End  ***"
  echo "*******************"
