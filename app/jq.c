#include <stdio.h>
#include <string.h>

#define ZSV_COMMAND_NO_OPTIONS
#define ZSV_COMMAND jq
#include "zsv_command.h"

#include <zsv/utils/jq.h>

/**
 * This implementation is not intended to replicate the full functionality of the `jq`
 * utility available at https://github.com/stedolan/jq
 *
 * This `jq` implementation is included as a convenience so as not to require a
 * separate `jq` installation for basic JSON parsing and manipulation
 */
int ZSV_MAIN_NO_OPTIONS_FUNC(ZSV_COMMAND)(int argc, const char *argv[]) {
  if(argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
    printf("Usage: " APPNAME " <filter> filename [-o,--output filename] [--csv]\n");
    return 0;
  }

  char to_csv = 0;
#ifdef NO_STDIN
  const int min_args = 3;
#else
  const int min_args = 2;
#endif
  if(argc < min_args) {
    fprintf(stderr, "Please provide a filter and an input file\n");
    return 1;
  }

#ifdef NO_STDIN
  FILE *f_in = NULL;
#else
  FILE *f_in = stdin;
#endif

  FILE *f_out = stdout;

  int err = 0;
  const unsigned char *jqfilter = (const unsigned char *)argv[1];

  for(int i = 2; !err && i < argc; i++) { // jq filter filename
    const char *arg = argv[i];
    if(i == 2 && *arg != '-') {
      if(!(f_in = fopen(arg, "rb"))) {
        err = 1;
        fprintf(stderr, "Unable to open for read: %s\n", arg);
      }
    } else if(!strcmp(arg, "--csv")) {
      to_csv = 1;
    } else if(!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
      i++;
      if(!(i < argc)) {
        err = 1;
        fprintf(stderr, "Option %s requires a filename\n", arg);
      } else if(!(f_out = fopen(argv[i], "wb"))) {
        err = 1;
        fprintf(stderr, "Unable to open for write: %s\n", argv[i]);
      }
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", arg);
      err = 1;
    }
  }

  if(!f_in) {
    fprintf(stderr, "Please specify an input file\n");
    err = 1;
  }

  if(!err) {
    void (*jqfunc)(jv, void *) = to_csv ? jv_to_csv : jv_to_json_func;
    struct jv_to_json_ctx ctx;
    ctx.write1 = zsv_jq_fwrite1;
    ctx.ctx = f_out;
    ctx.flags = JV_PRINT_PRETTY | JV_PRINT_SPACE1;

    void *jqctx = to_csv ? (void *)f_out : (void *)&ctx;
    enum zsv_jq_status jqstat;
    zsv_jq_handle zjq = zsv_jq_new(jqfilter, jqfunc, jqctx, &jqstat);
    if(jqstat != zsv_jq_status_ok) {
      fprintf(stderr, "Invalid filter: %s\n", jqfilter);
      err = 1;
    } else {
      if((jqstat = zsv_jq_parse_file(zjq, f_in)))
        err = jqstat;
      if((jqstat = zsv_jq_finish(zjq)) && !err)
        err = jqstat;
      if(!err && !to_csv)
        fprintf(f_out, "\n");
    }
    zsv_jq_delete(zjq);
  }

  if(f_out && f_out != stdout)
    fclose(f_out);
  if(f_in && f_in != stdin)
    fclose(f_in);

  return err;
}
