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
char havearg(const char *arg, const char *form1, size_t min_len1, const char *form2, size_t min_len2);

/**
 * set or get default parser options
 */
void zsv_set_default_opts(struct zsv_opts);

struct zsv_opts zsv_get_default_opts(void);

void zsv_clear_default_opts(void);

#ifdef ZSV_EXTRAS

/**
 * set the default option progress callback (e.g. from wasm where `struct zsv_opts`
 * cannot be independently accessed)
 * @param cb callback to call
 * @param ctx pointer passed to callback
 * @param frequency number of rows to parse between progress calls
 */
void zsv_set_default_progress_callback(zsv_progress_callback cb, void *ctx, size_t rows_interval,
                                       unsigned int seconds_interval);

/**
 * set the default option completed callback (e.g. from wasm where `struct zsv_opts`
 * cannot be independently accessed)
 * @param cb callback to call
 * @param ctx pointer passed to callback
 */
void zsv_set_default_completed_callback(zsv_completed_callback cb, void *ctx);

#endif

/**
 * Convert common command-line arguments to zsv_opts
 * Return new argc/argv values with processed args stripped out, each arg
 * matched by zsv_arg_to_opts(). Every arg is scanned, "--" and the values of
 * the command's own options included, since only the command knows which args
 * those are; a command that takes arbitrary option values calls
 * zsv_arg_to_opts() from its own parser instead.
 * Initializes opts_out with `zsv_get_default_opts()`, then with
 * the below common options if present:
 *     -B,--buff-size <N>
 *     -c,--max-column-count <N>
 *     -r,--max-row-size <N>
 *     -t,--tab-delim
 *     -O,--other-delim <C>
 *     -q,--no-quote
 *     -R,--skip-head <n>: skip n initial rows
 *     -S,--keep-blank-headers: disable default behavior of ignoring leading blank rows
 *     -d,--header-row-span <n>: apply header depth (rowspan) of n
 *     --stdin-filename <path>: apply saved file properties associated with
 *         the given path to input read from stdin
 *     -v,--verbose
 *     -u,--malformed-utf8-replacement <string>
 *     -0,--header-row <header>
 *     -1,--apply-overwrites, -L,--limit-rows <n> (ZSV_EXTRAS only)
 *     --only-crlf (unless ZSV_NO_ONLY_CRLF), --parser <default|fast|compat>
 *
 * @param  argc      count of args to process
 * @param  argv      args to process
 * @param  argc_out  count of unprocessed args
 * @param  argv_out  array of unprocessed arg values. Must be allocated by caller
 *                   with size of at least argc * sizeof(*argv)
 * @param  opts_out  options, updated to reflect any processed args
 * @return           zero on success, non-zero on error
 */
enum zsv_status zsv_args_to_opts(int argc, const char *argv[], int *argc_out, const char **argv_out,
                                 struct zsv_opts *opts_out);

enum zsv_arg_match {
  zsv_arg_none = 0, /* not a common option: the caller handles argv[*arg_i] */
  zsv_arg_ok,       /* applied to opts */
  zsv_arg_err       /* invalid, with the message on stderr */
};

/**
 * Match argv[*arg_i] against the common options zsv_args_to_opts() handles and
 * apply it to opts. For a command that parses its own args: call it where the
 * command's own options fail to match, so the value of one of its options is
 * never read as a common option.
 *
 * "-" (stdin), "--" and non-option args are zsv_arg_none. On zsv_arg_ok and
 * zsv_arg_err, *arg_i is left at the last arg consumed (the option's value, if
 * it takes one), so the caller's loop increment moves past it.
 *
 * @param  argc   count of args
 * @param  argv   args
 * @param  arg_i  index of the arg to match; updated as above
 * @param  opts   options to update; the caller initializes them, e.g. with
 *                zsv_get_default_opts()
 */
enum zsv_arg_match zsv_arg_to_opts(int argc, const char *argv[], int *arg_i, struct zsv_opts *opts);

/**
 * Fetch the next arg, if it exists, else print an error message
 * The argc_i argument does not need to be valid; it will be checked
 * against argc.
 *
 * Example:
 *   const char *value = zsv_next_arg(++arg_i, argc, argv, &err);
 *
 * @param  argc_i index of next arg to test
 * @param  argc   total arg count
 * @param  argv   args to process
 * @param  err    return non-zero in the case of error
 * @return        next argument, if it exists, else NULL
 */
const char *zsv_next_arg(int arg_i, int argc, const char *argv[], int *err);

/**
 * zsv_arg_is_option: true for an option-shaped token ('-x', '-xyz', '--name');
 * false for a lone "-" (stdin) and for non-dash tokens. Use at a command's
 * "treat as input" fall-through to detect a token that no option branch matched.
 */
int zsv_arg_is_option(const char *arg);

/**
 * zsv_err_unrecognized_option: print the canonical unrecognized-option
 * diagnostic ("Unrecognized option: <arg>") to stderr and return the uniform
 * nonzero exit status used across all commands.
 */
int zsv_err_unrecognized_option(const char *arg);

#endif
