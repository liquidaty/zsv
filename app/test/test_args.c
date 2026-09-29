// Unit test for zsv_arg_to_opts() and zsv_args_to_opts(): the common options, matched one
// arg at a time. Exit status is the number of failed checks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zsv.h>
#include <zsv/utils/arg.h>

static int failures;

#define CHECK(cond, ...)                                                                                               \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      failures++;                                                                                                      \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                                             \
      fprintf(stderr, __VA_ARGS__);                                                                                    \
      fputc('\n', stderr);                                                                                             \
    }                                                                                                                  \
  } while (0)

#define ARGC(a) ((int)(sizeof(a) / sizeof(*(a))))

// the fields an option sets
static int buffsize(const struct zsv_opts *o) {
  return o->buffsize == 8192;
}
static int max_columns(const struct zsv_opts *o) {
  return o->max_columns == 9 && o->option_overrides.max_column_count;
}
static int max_row_size(const struct zsv_opts *o) {
  return o->max_row_size == ZSV_ROW_MAX_SIZE_MIN;
}
static int tab(const struct zsv_opts *o) {
  return o->delimiter == '\t';
}
static int other_delim(const struct zsv_opts *o) {
  return o->delimiter == ';';
}
static int no_quotes(const struct zsv_opts *o) {
  return o->no_quotes;
}
static int verbose(const struct zsv_opts *o) {
  return o->verbose;
}
static int skip_head(const struct zsv_opts *o) {
  return o->rows_to_ignore == 3 && o->option_overrides.skip_head;
}
static int header_span(const struct zsv_opts *o) {
  return o->header_span == 2 && o->option_overrides.header_row_span;
}
static int keep_blank(const struct zsv_opts *o) {
  return o->keep_empty_header_rows;
}
static int utf8_none(const struct zsv_opts *o) {
  return o->malformed_utf8_replace == ZSV_MALFORMED_UTF8_DO_NOT_REPLACE &&
         o->option_overrides.malformed_utf8_replacement;
}
static int utf8_remove(const struct zsv_opts *o) {
  return o->malformed_utf8_replace == ZSV_MALFORMED_UTF8_REMOVE;
}
static int header_row(const struct zsv_opts *o) {
  return o->insert_header_row && !strcmp(o->insert_header_row, "a,b");
}
static int overwrite_auto(const struct zsv_opts *o) {
  return o->overwrite_auto;
}
static int max_rows(const struct zsv_opts *o) {
  return o->max_rows == 5;
}
static int only_crlf(const struct zsv_opts *o) {
  return o->only_crlf_rowend;
}
static int stdin_filename(const struct zsv_opts *o) {
  return o->stdin_filename && !strcmp(o->stdin_filename, "a.csv");
}
static int parser_fast(const struct zsv_opts *o) {
  return o->scan_engine == 3;
}
static int parser_compat(const struct zsv_opts *o) {
  return o->scan_engine == 255;
}

#define MAX_ARGS 4

struct match_case {
  const char *argv[MAX_ARGS]; // argv[0] is the command; zsv_arg_to_opts() starts at argv[1]
  enum zsv_arg_match want;
  int want_end;
  int (*applied)(const struct zsv_opts *);
};

static const struct match_case match_cases[] = {
  {{"cmd", "-B", "8192"}, zsv_arg_ok, 2, buffsize},
  {{"cmd", "--buff-size", "8192"}, zsv_arg_ok, 2, buffsize},
  {{"cmd", "-c", "9"}, zsv_arg_ok, 2, max_columns},
  {{"cmd", "--max-column-count", "9"}, zsv_arg_ok, 2, max_columns},
  {{"cmd", "-r", "1024"}, zsv_arg_ok, 2, max_row_size},
  {{"cmd", "--max-row-size", "1024"}, zsv_arg_ok, 2, max_row_size},
  {{"cmd", "-t", "x"}, zsv_arg_ok, 1, tab},
  {{"cmd", "--tab-delim"}, zsv_arg_ok, 1, tab},
  {{"cmd", "-O", ";"}, zsv_arg_ok, 2, other_delim},
  {{"cmd", "--other-delim", ";"}, zsv_arg_ok, 2, other_delim},
  {{"cmd", "-q"}, zsv_arg_ok, 1, no_quotes},
  {{"cmd", "--no-quote"}, zsv_arg_ok, 1, no_quotes},
  {{"cmd", "-v"}, zsv_arg_ok, 1, verbose},
  {{"cmd", "--verbose", "x"}, zsv_arg_ok, 1, verbose},
  {{"cmd", "-R", "3", "x"}, zsv_arg_ok, 2, skip_head},
  {{"cmd", "--skip-head", "3"}, zsv_arg_ok, 2, skip_head},
  {{"cmd", "-d", "2"}, zsv_arg_ok, 2, header_span},
  {{"cmd", "--header-row-span", "2"}, zsv_arg_ok, 2, header_span},
  {{"cmd", "-S"}, zsv_arg_ok, 1, keep_blank},
  {{"cmd", "--keep-blank-headers"}, zsv_arg_ok, 1, keep_blank},
  {{"cmd", "-u", "none"}, zsv_arg_ok, 2, utf8_none},
  {{"cmd", "--malformed-utf8-replacement", ""}, zsv_arg_ok, 2, utf8_remove},
  {{"cmd", "-0", "a,b"}, zsv_arg_ok, 2, header_row},
  {{"cmd", "--header-row", "a,b"}, zsv_arg_ok, 2, header_row},
  {{"cmd", "-1"}, zsv_arg_ok, 1, overwrite_auto},
  {{"cmd", "--apply-overwrites"}, zsv_arg_ok, 1, overwrite_auto},
  {{"cmd", "-L", "5"}, zsv_arg_ok, 2, max_rows},
  {{"cmd", "--limit-rows", "5"}, zsv_arg_ok, 2, max_rows},
  {{"cmd", "--only-crlf", "x"}, zsv_arg_ok, 1, only_crlf},
  {{"cmd", "--stdin-filename", "a.csv"}, zsv_arg_ok, 2, stdin_filename},
  {{"cmd", "--parser", "fast"}, zsv_arg_ok, 2, parser_fast},
  {{"cmd", "--parser", "compat"}, zsv_arg_ok, 2, parser_compat},
  // not common options: the index stays
  {{"cmd", "-"}, zsv_arg_none, 1, NULL},
  {{"cmd", "--"}, zsv_arg_none, 1, NULL},
  {{"cmd", "file"}, zsv_arg_none, 1, NULL},
  {{"cmd", ""}, zsv_arg_none, 1, NULL},
  {{"cmd", "--nope"}, zsv_arg_none, 1, NULL},
  {{"cmd", "-z"}, zsv_arg_none, 1, NULL},
  {{"cmd", "-RX"}, zsv_arg_none, 1, NULL},
  // errors: the index is left at the last arg consumed
  {{"cmd", "-R"}, zsv_arg_err, 1, NULL},
  {{"cmd", "-B", "1"}, zsv_arg_err, 2, NULL},
  {{"cmd", "-O", ";;"}, zsv_arg_err, 2, NULL},
  {{"cmd", "-u", "xyz"}, zsv_arg_err, 2, NULL},
  {{"cmd", "-0", ""}, zsv_arg_err, 2, NULL},
  {{"cmd", "-L", "0"}, zsv_arg_err, 2, NULL},
  {{"cmd", "--stdin-filename"}, zsv_arg_err, 1, NULL},
  {{"cmd", "--stdin-filename", ""}, zsv_arg_err, 2, NULL},
  {{"cmd", "--stdin-filename", "-"}, zsv_arg_err, 2, NULL},
  {{"cmd", "--parser"}, zsv_arg_err, 1, NULL},
  {{"cmd", "--parser", "slow"}, zsv_arg_err, 2, NULL},
};

static void test_match(void) {
  for (size_t k = 0; k < sizeof(match_cases) / sizeof(*match_cases); k++) {
    const struct match_case *c = &match_cases[k];
    int argc = 0;
    while (argc < MAX_ARGS && c->argv[argc])
      argc++;
    struct zsv_opts o = zsv_get_default_opts();
    int end = 1;
    const char *argv[MAX_ARGS];
    memcpy(argv, c->argv, sizeof(argv));
    enum zsv_arg_match m = zsv_arg_to_opts(argc, argv, &end, &o);
    CHECK(m == c->want && end == c->want_end && (!c->applied || c->applied(&o)),
          "%s %s: got match %d, index %d; want %d, %d%s", c->argv[1], argc > 2 ? c->argv[2] : "", (int)m, end,
          (int)c->want, c->want_end, c->applied ? ", applied" : "");
  }

  struct zsv_opts o = zsv_get_default_opts();
  const char *past[] = {"cmd"};
  int end = 1;
  CHECK(zsv_arg_to_opts(1, past, &end, &o) == zsv_arg_none && end == 1, "an index past argc: none");

  // "-" on the heap: a read past its terminator is a sanitizer report
  char *dash = malloc(2);
  if (dash) {
    memcpy(dash, "-", 2);
    const char *stdin_arg[] = {"cmd", dash};
    end = 1;
    CHECK(zsv_arg_to_opts(2, stdin_arg, &end, &o) == zsv_arg_none && end == 1, "a heap \"-\": none");
    free(dash);
  }
}

// A command whose own "--label <text>" is parsed first: its value is never a common option
static void test_command_owns_its_values(void) {
  const char *argv[] = {"cmd", "--label", "-1", "-R", "2", "--label", "--verbose"};
  const char *labels[2] = {0};
  int nlabels = 0;
  struct zsv_opts o = zsv_get_default_opts();
  enum zsv_arg_match m = zsv_arg_none;
  for (int i = 1; m != zsv_arg_err && i < ARGC(argv); i++) {
    if (!strcmp(argv[i], "--label") && i + 1 < ARGC(argv))
      labels[nlabels++] = argv[++i];
    else
      m = zsv_arg_to_opts(ARGC(argv), argv, &i, &o);
  }
  CHECK(m != zsv_arg_err && nlabels == 2 && !strcmp(labels[0], "-1") && !strcmp(labels[1], "--verbose"),
        "--label values -1 and --verbose kept");
  CHECK(o.rows_to_ignore == 2 && !o.verbose, "-R 2 applied; the --verbose value did not set verbose");
#ifdef ZSV_EXTRAS
  CHECK(!o.overwrite_auto, "the -1 value did not set --apply-overwrites");
#endif
}

// zsv_args_to_opts() on argv: 1 when it succeeds and leaves exactly want[0..nwant-1]
static int strips_to(int argc, const char *argv[], const char *want[], int nwant, struct zsv_opts *opts) {
  const char *out[16];
  int n = -1;
  if (zsv_args_to_opts(argc, argv, &n, out, opts) != zsv_status_ok || n != nwant)
    return 0;
  for (int i = 0; i < n; i++)
    if (strcmp(out[i], want[i]))
      return 0;
  return 1;
}

static void test_args_to_opts(void) {
  struct zsv_opts o;

  const char *anywhere[] = {"cmd", "-R", "2", "file", "--foo", "-t"};
  const char *anywhere_out[] = {"cmd", "file", "--foo"};
  CHECK(strips_to(ARGC(anywhere), anywhere, anywhere_out, ARGC(anywhere_out), &o) && o.rows_to_ignore == 2 &&
          o.delimiter == '\t',
        "common options stripped anywhere, the rest kept in order");

  // "--" can be a command's option value (lq desc+ --strat-definition --), so the scan goes on past it
  const char *dashdash[] = {"cmd", "-q", "--", "-R", "2", "x"};
  const char *dashdash_out[] = {"cmd", "--", "x"};
  CHECK(strips_to(ARGC(dashdash), dashdash, dashdash_out, ARGC(dashdash_out), &o) && o.no_quotes &&
          o.rows_to_ignore == 2,
        "-- passes through and the scan goes on");

  const char *dash[] = {"cmd", "-", "-v"};
  const char *dash_out[] = {"cmd", "-"};
  CHECK(strips_to(ARGC(dash), dash, dash_out, ARGC(dash_out), &o) && o.verbose, "- passes through");

  // in place, as cli.c calls it
  const char *in_place[] = {"cmd", "a", "-R", "1", "b", "-S", "c"};
  int n = 0;
  CHECK(zsv_args_to_opts(ARGC(in_place), in_place, &n, in_place, &o) == zsv_status_ok && n == 4 &&
          !strcmp(in_place[1], "a") && !strcmp(in_place[2], "b") && !strcmp(in_place[3], "c") &&
          o.keep_empty_header_rows,
        "in place: a b c kept");

  // the scan stops at an error: the -R after it is not applied
  const char *stops[] = {"cmd", "--parser", "slow", "-R", "2"};
  const char *stops_out[4];
  n = 0;
  CHECK(zsv_args_to_opts(ARGC(stops), stops, &n, stops_out, &o) == zsv_status_error && o.rows_to_ignore == 0,
        "the scan stops at an error");

  const char *missing[] = {"cmd", "x", "-c"};
  const char *out[4];
  CHECK(zsv_args_to_opts(ARGC(missing), missing, &n, out, &o) == zsv_status_error, "-c without a value: error");
}

int main(void) {
  test_match();
  test_command_owns_its_values();
  test_args_to_opts();
  if (failures)
    fprintf(stderr, "%d check(s) failed\n", failures);
  return failures;
}
