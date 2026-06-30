/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_UTILS_APPNAME_H
#define ZSV_UTILS_APPNAME_H

#include <stdio.h> /* FILE */

/*
 * The zsv commands can be bundled into the `zsv` CLI, built standalone, or
 * embedded into a third-party app. Their help/usage text must therefore refer
 * to the host program by its *runtime* name (e.g. "zsv" or "xxx"), not a name
 * fixed at compile time.
 *
 * Usage strings embed the ZSV_USAGE_PROG placeholder wherever the host program
 * name belongs; zsv_print_usage() substitutes the runtime name when printing.
 * The subcommand portion ("compare", "count", ...) is supplied separately via
 * the APPNAME macro (see zsv_command.h), so a single token suffices here.
 */

/* Compile-time fallback; an embedder may override with
 *   -DZSV_DEFAULT_PROG_NAME='"xxx"' */
#ifndef ZSV_DEFAULT_PROG_NAME
#define ZSV_DEFAULT_PROG_NAME "zsv"
#endif

/* Placeholder for the host program name within usage[] string literals.
 * Uses a control character that never appears in legitimate help text. */
#define ZSV_USAGE_PROG "\x01"

/* Set the host program name once at startup, from argv[0] (the basename is
 * extracted) or an explicit override. host may be NULL or empty, in which case
 * the current value (default ZSV_DEFAULT_PROG_NAME) is retained. */
void zsv_set_prog_name(const char *host);

/* The host program name, e.g. "zsv" or "xxx". Never NULL. */
const char *zsv_prog_name(void);

/* Print a NULL-terminated array of usage lines to the given stream, substituting
 * ZSV_USAGE_PROG with zsv_prog_name() and appending a newline to each line. */
void zsv_fprint_usage(FILE *f, const char *const *lines);

/* Convenience wrapper for zsv_fprint_usage(stdout, lines). */
void zsv_print_usage(const char *const *lines);

#endif
