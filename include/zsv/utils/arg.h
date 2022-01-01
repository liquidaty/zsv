/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_ARG_H
#define ZSV_ARG_H

#include <zsv/common.h>

/* havearg(): case-insensitive partial arg matching */
char havearg(const char *arg,
             const char *form1, size_t min_len1,
             const char *form2, size_t min_len2);

/**
 * set or get default parser options
 */
void zsv_set_default_opts(struct zsv_opts);

struct zsv_opts zsv_get_default_opts();

/**
 * process common argc/argv options and return new argc/argv values
 * with processed args stripped out. Initializes opts_out with
 * `zsv_get_default_opts()`, then with the below common options if present:
 *     -B,--buff-size <N>
 *     -c,--max-column-count <N>
 *     -r,--max-row-size <N>
 *     -t,--tab-delim
 *     -O,--other-delim <C>
 *     -q,--no-quote
 *     -v,--verbose
 *
 * @param  argc     count of args to process
 * @param  argv     args to process
 * @param  argc_out count of unprocessed args
 * @param  argv_out array of unprocessed arg values. Must be allocated by caller
 *                  with size of at least argc * sizeof(*argv)
 * @param  opts_out options, updated to reflect any processed args
 * @return          zero on success, non-zero on error
 */
int zsv_args_to_opts(int argc, const char *argv[],
                     int *argc_out, const char **argv_out,
                     struct zsv_opts *opts_out
                     );

#define INIT_DEFAULT_ARGS() do {                                \
    struct zsv_opts otmp;                                       \
    int err = zsv_args_to_opts(argc, argv, &argc, argv, &otmp); \
    if(err) return err;                                         \
    else zsv_set_default_opts(otmp);                            \
  } while(0)

#ifdef ZSV_CLI
# define INIT_CMD_DEFAULT_ARGS()
#else
# define INIT_CMD_DEFAULT_ARGS() INIT_DEFAULT_ARGS()
#endif

#endif
