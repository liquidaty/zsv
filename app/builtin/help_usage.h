static const char *usage[] = {
  ZSV_USAGE_PROG ": streaming csv processor",
  "",
  "Usage:",
  "  " ZSV_USAGE_PROG " version: display version info (and if applicable, extension info)",
#ifndef __EMSCRIPTEN__
  "  " ZSV_USAGE_PROG " (un)register [<extension_id>]    : (un)register an extension",
  "      Registration info is saved in zsv.ini located in a directory determined as:",
  "        ZSV_CONFIG_DIR environment variable value, if set",
#if defined(_WIN32)
  "        LOCALAPPDATA environment variable value, if set",
  "        otherwise, C:\\temp",
#else
  "        otherwise, " PREFIX "/etc",
#endif
#endif
  "  " ZSV_USAGE_PROG " help [<command>]",
  "  " ZSV_USAGE_PROG " <command> <options> <arguments>  : run a command on data (see below for details)",
#ifndef __EMSCRIPTEN__
  "  " ZSV_USAGE_PROG " <id>-<cmd> <options> <arguments> : invoke command 'cmd' of extension 'id'",
  "  " ZSV_USAGE_PROG " license [<extension_id>]",
#endif
  "  " ZSV_USAGE_PROG " thirdparty                       : view third-party licenses & acknowledgements",
  NULL,
};
static const char *common_options_title = "Options common to all commands except `prop`, `rm` and `jq`:";
static const char *common_options[] = {
#ifdef ZSV_EXTRAS
  "  -L,--limit-rows <n>      : limit processing to the given number of rows (including any header row(s))",
#endif
  "  -c,--max-column-count <n>: set the maximum number of columns parsed per row. defaults to 1024",
  "  -r,--max-row-size <n>    : set the minimum supported maximum row size. defaults to 64k",
  "  -B,--buff-size <n>       : set internal buffer size. defaults to 256k",
  "  -t,--tab-delim           : set column delimiter to tab",
  "  -O,--other-delim <char>  : set column delimiter to specified character",
  "  -q,--no-quote            : turn off quote handling",
  "  -R,--skip-head <n>       : skip specified number of initial rows",
  "  -d,--header-row-span <n> : apply header depth (rowspan) of n",
  "  -u,--malformed-utf8-replacement <string>: replacement string (can be empty) in case of malformed UTF8 input",
  "       (default for \"desc\" command is '?')",
  "  -S,--keep-blank-headers  : disable default behavior of ignoring leading blank rows",
  "  -0,--header-row <header> : insert the provided CSV as the first row (in position 0)",
  "                             e.g. --header-row 'col1,col2,\"my col 3\"'",
  "  --stdin-filename <path>  : apply saved file properties associated with the given path",
  "                             to input read from stdin",
#ifndef ZSV_NO_ONLY_CRLF
  "  --only-crlf              : only treat CRLF as row delimiter",
  "                             CR or LF alone are treated as normal chars that do not require quotes",
#endif
#ifdef ZSV_EXTRAS
  "  -1,--apply-overwrites    : automatically apply overwrites saved via `overwrite` command",
#endif
  "  --parser <default|fast|compat>",
  "                           : select parser engine. 'fast' uses branchless SIMD",
  "                             (aarch64 NEON or x86-64 AVX2/SSE2).",
  "                             'compat' uses the scalar engine that works with",
  "                             all CSV including non-4180-compliant quoting",
  "  -v,--verbose             : verbose output",
  NULL,
};

static const char *commands_title = "Commands that parse CSV or other tabular data:";

/* Structured command catalog: the single source of truth for each
 * command's name and one-line synopsis. help.c formats this into the
 * display list shown by `zsv help`; downstream tools (e.g. the lq CLI)
 * include this header and look up a command's synopsis by name, so the
 * text lives in exactly one place rather than being duplicated. A NULL
 * `name` marks a section break whose `synopsis`, if non-NULL, is printed
 * as a sub-heading. The array is terminated by a {NULL, NULL} sentinel. */
struct zsv_help_command {
  const char *name;     /* command name; NULL marks a section break */
  const char *synopsis; /* one-line description (or sub-heading text) */
};
static const struct zsv_help_command commands[] = {
  {"echo", "write tabular input to stdout with optional cell overwrites"},
  {"check", "check for anomalies (column counts, utf8 encoding etc)"},
  {"count", "print the number of rows"},
  {"select", "extract rows/columns by name or position and perform other basic and 'cleanup' operations"},
  {"desc", "describe each column"},
  {"sql", "run ad-hoc SQL on one or more CSV files"},
  {"pretty", "pretty print for console display"},
  {"serialize", "convert into 3-column format (id, column name, cell value)"},
  {"flatten",
   "flatten a table consisting of N groups of data, each with 1 or more rows in the table, into a table of N rows"},
  {"2json", "convert CSV or sqlite3 db table to json"},
#ifndef ZSV_NO_TOON
  {"2toon", "convert CSV or sqlite3 db table to TOON"},
#endif
  {"2tsv", "convert to tab-delimited text"},
  {"stack", "stack tables vertically, aligning columns with common names"},
  {"paste", "horizontally paste two tables together: given inputs X, Y, ... of N rows"},
  {"compare", "compare two or more tables and output differences"},
  {"overwrite", "save, modify or apply overwrites"},
  {NULL, "Other commands:"},
  {"2db", "convert json to sqlite3 db"},
  {"prop",
   "save parsing options associated with a file that are subsequently applied by default when processing that file"},
  {"rm", "remove a file and its related cache"},
  {"mv", "rename (move) a file and/or its related cache"},
#ifdef USE_JQ
  {"jq", "run a jq filter on json input"},
#endif
  {NULL, NULL},
};
