#ifndef ZSV_MAIN_H
#define ZSV_MAIN_H

/**
 * ZSV commands can each be compiled as either a standalone executable or a
 * command bundled into the `zsv` CLI. To support these different options
 * without repeating common invocation code, we define two sets of macros: one
 * set each for commands that do, and that do not, use common zsv parsing
 * options. Each set has a macro each for the entry point name and declaration
 */

#define ZSV_MAIN_FUNC1(x) zsv_ ## x ## _main
#define ZSV_MAIN_NO_OPTIONS_FUNC1(x) zsv_ ## x ## _main_no_options

struct zsv_opts;
struct zsv_prop_handler;

/* macros for commands that use common zsv parsing */
#define ZSV_MAIN_FUNC(x) ZSV_MAIN_FUNC1(x)
#define ZSV_MAIN_DECL(x) int ZSV_MAIN_FUNC(x)(int argc, const char *argv[], struct zsv_opts *opts, struct zsv_prop_handler *custom_prop_handler, const char *opts_used)

/* macros for commands that do not use common zsv parsing */
#define ZSV_MAIN_NO_OPTIONS_FUNC(x) ZSV_MAIN_NO_OPTIONS_FUNC1(x)
#define ZSV_MAIN_NO_OPTIONS_DECL(x) int ZSV_MAIN_NO_OPTIONS_FUNC(x)(int argc, const char *argv[])

#endif
