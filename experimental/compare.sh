#!/bin/sh

# Note - assumes:
#   - zsv_main is the zsv executable compiled from the main branch
#   - worldcitiespop_cr.csv, worldcitiespop_crlf.csv, worldcitiespop_lf.csv have been prepared locally by running prepare_data.sh

# Run with:
#   sh compare.txt
# or (if wanting to redirect all output to a file):
#.   sh compare.sh > compare.txt 2>&1

  echo "*********************"
  echo "*** Test - Start  ***"
  echo "*********************"

# compare count times and values
  echo
  echo "Running count tests with LF..." 
  echo
  echo "> count-neon-columns-callback-v3 > LF:" 
  time ./count-neon-columns-callback-v3 < worldcitiespop_lf.csv
  echo "> zsv_main > LF:" 
  time zsv_main count < worldcitiespop_lf.csv
  echo "> zsv > LF:" 
  time zsv count < worldcitiespop_lf.csv
  echo 
  echo "Running count tests with CRLF..." 
  echo
  echo "> count-neon-columns-callback-v3 > CRLF:" 
  time ./count-neon-columns-callback-v3 < worldcitiespop_crlf.csv
  echo "> zsv_main > CRLF:" 
  time zsv_main count < worldcitiespop_crlf.csv
  echo "> zsv > CRLF:" 
  time zsv count < worldcitiespop_crlf.csv
  echo
  echo "Running count tests with CR..." 
  echo
  echo "> count-neon-columns-callback-v3 > CR:" 
  time ./count-neon-columns-callback-v3 < worldcitiespop_cr.csv
  echo "> zsv_main > CR:"   
  time zsv_main count < worldcitiespop_cr.csv
  echo "> zsv > CR:"   
  time zsv count < worldcitiespop_cr.csv
# # compare select times
  echo
  echo "Running select tests with LF..." 
  echo
  echo "> count-neon-columns-callback-v3 > LF:" 
  time ./count-neon-columns-callback-v3 test1 < worldcitiespop_lf.csv >/dev/null
  echo "> zsv_main > LF:" 
  time zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_lf.csv >/dev/null
  echo "> zsv > LF:" 
  time zsv select -n -- 1 2 3 6 7 < worldcitiespop_lf.csv >/dev/null
  echo
  echo "Running select tests with CRLF..." 
  echo
  echo "> count-neon-columns-callback-v3 > CRLF:" 
  time ./count-neon-columns-callback-v3 test1 < worldcitiespop_crlf.csv >/dev/null
  echo "> zsv_main > CRLF:" 
  time zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_crlf.csv >/dev/null
  echo "> zsv > CRLF:" 
  time zsv select -n -- 1 2 3 6 7 < worldcitiespop_crlf.csv >/dev/null
  echo
  echo "Running select tests with CR..." 
  echo
  echo "> count-neon-columns-callback-v3 > CR:" 
  time ./count-neon-columns-callback-v3 test1 < worldcitiespop_cr.csv >/dev/null
  echo "> zsv_main > CR:" 
  time zsv_main select -n -- 1 2 3 6 7 < worldcitiespop_cr.csv >/dev/null
  echo "> zsv > CR:" 
  time zsv select -n -- 1 2 3 6 7 < worldcitiespop_cr.csv >/dev/null

  echo "*******************"
  echo "*** Test - End  ***"
  echo "*******************"
