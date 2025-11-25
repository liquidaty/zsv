#!/usr/bin/awk -f

BEGIN {
    # Output CSV Header
    OFS = ","
    print "operation,dataset,tool,real_sec,user_sec,sys_sec"
}

# 1. Capture State: Detect which benchmark is running
# Matches lines like: "  Benchmarking count: worldcitiespop_mil"
/Benchmarking/ {
    # $2 is "count:", $3 is "worldcitiespop_mil"
    gsub(/:/, "", $2)  # Remove colon from operation name
    op = $2
    dataset = $3
}

# 2. Capture Data: Process lines with timing info
# Matches lines containing ": real" (e.g., "zsv : real 0m0.056s...")
/: real/ {
    # Split line by the first colon to separate Tool from Data
    split($0, parts, ":")
    
    # Clean up the tool name (trim whitespace)
    tool = parts[1]
    gsub(/^[ \t]+|[ \t]+$/, "", tool)

    # The rest of the line (parts[2]) contains: " real 0m0.56s user ..."
    # We split this substring by space to find the times
    split(parts[2], tokens, " ")
    
    # Iterate through tokens to find 'real', 'user', 'sys' values
    for (i = 1; i <= length(tokens); i++) {
        if (tokens[i] == "real") real = parse_time(tokens[i+1])
        if (tokens[i] == "user") user = parse_time(tokens[i+1])
        if (tokens[i] == "sys")  sys_val = parse_time(tokens[i+1])
    }

    # Print the CSV row
    print op, dataset, tool, real, user, sys_val
}

# Helper: Convert "XmY.Ys" string to seconds (float)
function parse_time(t_str,    arr, min, sec) {
    # Split "0m0.056s" at "m"
    split(t_str, arr, "m")
    min = arr[1]
    
    # Remove trailing 's' from the seconds part
    sec = arr[2]
    gsub(/s/, "", sec)
    
    return (min * 60) + sec
}
