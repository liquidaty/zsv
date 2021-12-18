#ifndef CLI_CMD_INTERNAL_H
#define CLI_CMD_INTERNAL_H

/**
 * process common argc/argv options and return new argc/argv values
 * with processed args stripped out
 *
 * strip out the first argument as well as common options:
 *     -B,--buff-size <N>
 *     -c,--max-column-count <N>
 *     -r,--max-row-size <N>
 *     -t,--tab-delim
 *     -O,--other-delim <C>
 *     -q,--no-quote
 *     -v,--verbose
 *
 * @param argc count of args to process
 * @param argv args to process
 * @param argc_out count of unprocessed args
 * @param argv_out array of unprocessed arg values. Must be free'd
 * @param opts_out options, updated to reflect any processed args
 * @return zero on success, non-zero on error
 */
int cli_args_to_opts(int argc, const char *argv[],
                     int *argc_out, const char ***argv_out,
                     struct zsv_opts *opts_out
                     );
#endif
