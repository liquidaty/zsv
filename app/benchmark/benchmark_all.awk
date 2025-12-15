#!/usr/bin/awk -f

BEGIN {
    # Output CSV Header
    OFS = ","
    print "operation,dataset,tool,real_sec,user_sec,sys_sec,max_rss_bytes,instr_retired"
}

# 1. Capture State
/Benchmarking/ {
    gsub(/:/, "", $2)
    op = $2
    dataset = $3
}

# 2. Capture Data
/maximum resident set size/ {
    # Isolate Tool Name
    split($0, parts, ":")
    tool = parts[1]
    gsub(/^[ \t]+|[ \t]+$/, "", tool)
    
    data_line = parts[2]

    # Initialize variables to 0/empty to prevent carry-over
    real=0; user=0; sys_val=0; rss=0; instr=0

    # -- Extraction Helper Logic --
    # In standard awk, match() sets RSTART and RLENGTH. 
    # We extract the whole matching phrase (e.g. "0.56 real") then split it.

    # 1. Real Time
    if (match(data_line, /[0-9.]+ +real/)) {
        str = substr(data_line, RSTART, RLENGTH)
        split(str, arr, " ")
        real = arr[1]
    }

    # 2. User Time
    if (match(data_line, /[0-9.]+ +user/)) {
        str = substr(data_line, RSTART, RLENGTH)
        split(str, arr, " ")
        user = arr[1]
    }

    # 3. Sys Time
    if (match(data_line, /[0-9.]+ +sys/)) {
        str = substr(data_line, RSTART, RLENGTH)
        split(str, arr, " ")
        sys_val = arr[1]
    }

    # 4. Max RSS (Memory)
    if (match(data_line, /[0-9]+ +maximum resident set size/)) {
        str = substr(data_line, RSTART, RLENGTH)
        split(str, arr, " ")
        rss = arr[1]
    }

    # 5. Instructions Retired (Not always present on non-Intel macs or Linux)
    if (match(data_line, /[0-9]+ +instructions retired/)) {
        str = substr(data_line, RSTART, RLENGTH)
        split(str, arr, " ")
        instr = arr[1]
    }

    print op, dataset, tool, real, user, sys_val, rss, instr
}