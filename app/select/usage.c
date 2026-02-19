const char *zsv_select_usage_msg[] = {
  APPNAME ": extracts and outputs specified columns",
  "",
  "Usage: " APPNAME " [filename] [options] [-- col_specifier [... col_specifier]]",
  "       where col_specifier is a column name or, if the -n option is used,",
  "       a column index (starting at 1) or index range in the form of n-m",
  "       e.g. " APPNAME " -n file.csv -- 1 4-6 50 10",
  "            " APPNAME " file.csv -- first_col fiftieth_column \"Tenth Column\"",
  "",
  "Note: Outputs the columns specified after '--' separator, or all columns if omitted.",
  "",
  "Options:",
  "  -b,--with-bom                : output with BOM",
  "  --fixed <offset1,offset2,..> : parse as fixed-width text; use given CSV list of positive integers for",
  "                                 cell and indexes",
  "  --fixed-auto                 : parse as fixed-width text; derive widths from first row in input data (max 256k)",
  "                                 assumes ASCII whitespace; multi-byte whitespace is not counted as whitespace",
  "  --fixed-auto-max-lines       : maximum number of lines to use in calculating fixed widths",
#ifndef ZSV_CLI
  "  -v,--verbose                 : verbose output",
#endif
  "  -H,--head <n>                : (head) only process the first n rows of input data (including header)",
  "  --skip-data <n>              : skip the specified number of data rows",
  "  --no-header                  : do not output header row",
  "  --prepend-header <value>     : prepend each column header with the given text <value>",
  "  -s,--search <value>          : only output rows with at least one cell containing <value>",
#ifdef HAVE_PCRE2_8
  "  --regex-search <pattern>     : only output rows with at least one cell matching the given regex pattern",
#endif
  // TO DO: " -s,--search /<pattern>/modifiers: search on regex pattern; modifiers include 'g' (global) and 'i'
  // (case-insensitive)",
  "  --sample-every <num_of_rows> : output a sample consisting of the first row, then every nth row",
  "  --sample-pct <percentage>    : output a randomly-selected sample (32 bits of randomness) of n%% of input rows",
  "  --distinct                   : skip subsequent occurrences of columns with the same name",
  "  --merge                      : merge subsequent occurrences of columns with the same name",
  "                                 outputting first non-null value",
  // --rename: like distinct, but instead of removing cols with dupe names, renames them, trying _<n> for n up to max
  // cols
  "  -e <embedded_lineend_char>   : char to replace embedded lineend. If left empty, embedded lineends are preserved.",
  "                                 If the provided string begins with 0x, it will be interpreted as the hex",
  "                                 representation of a string.",
  "  -x <column>                  : exclude the indicated column. can be specified more than once",
  "  -N,--line-number             : prefix each row with the row number",
  "  -n                           : provided column indexes are numbers corresponding to column positions",
  "                                 (starting with 1), instead of names",
#ifndef ZSV_CLI
  "  -T                           : input is tab-delimited, instead of comma-delimited",
  "  -O,--other-delim <delim>     : input is delimited with the given char",
  "                                 Note: This option does not support quoted values with embedded delimiters.",
#endif
  "  --unescape                   : escape any backslash-escaped input e.g. \\t, \\n, \\r such as output from `2tsv`",
  "  -w,--whitespace-clean        : normalize all whitespace to space or newline, single-char (non-consecutive)",
  "                                 occurrences",
  "  --whitespace-clean-no-newline: clean whitespace and remove embedded newlines",
  "  -W,--no-trim                 : do not trim whitespace",
#ifndef ZSV_CLI
  "  -C <max_num_of_columns>      : defaults to " ZSV_SELECT_MAX_COLS_DEFAULT_S,
  "  -L,--max-row-size <n>        : set the maximum memory used for a single row",
  "                                 Default: " ZSV_ROW_MAX_SIZE_MIN_S " (min), " ZSV_ROW_MAX_SIZE_DEFAULT_S " (max)",
#endif
#ifndef ZSV_NO_PARALLEL
  "  -j,--jobs <N>                : parallelize using N threads",
  "  --parallel                   : parallelize using a number of threads equal to the number of cores",
#endif
  "  -o <filename>                : filename to save output to",
  NULL,
};

static void zsv_select_usage(void) {
  for (size_t i = 0; zsv_select_usage_msg[i]; i++)
    fprintf(stdout, "%s\n", zsv_select_usage_msg[i]);
}
