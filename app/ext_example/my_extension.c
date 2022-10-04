/*
 * Copyright (C) 2021 Liquidaty and zsv contributors. All rights reserved.
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zsv/ext/implementation.h>
#include <zsv/utils/writer.h>

/**
 * `zsv` can easily be extended by simply creating a shared library
 * that implements the interface specified in zsv/ext/implementation.h
 * for any two-character extension id
 *
 * Once the library file is created, you can run any commands it implements
 * by naming the library file zsvext<id>, placing it in any folder that is
 * in the system lib search path, or in the same folder as zsv, and running:
 *    `zsv xx-command`
 *
 * It's as simple as that!
 *
 * In this example, we will implement two simple commands:
 * * `count` will count lines
 * * `echo` will regurgitate its input to stdout
 *
 * We will name our extension "my", so our shared library will be named
 * zsvextmy.so (non-win) or zsvextmy.dll (win). After the shared lib is built,
 * a user can place it anywhere in their path or in the same folder as the zsv
 * binary, and invoke our operations as follows:
 *   `zsv my-count`
 *   `zsv my-echo`
 *
 * in addition, users will see a brief description of our module if they execute:
 *   `zsv help`
 *
 * or
 *   `zsv help my-<command>`
 *
 */

/**
 * *Required*: define our extension id, which must be two characters in length
 */
const char *zsv_ext_id() {
  return "my";
}

/**
 * When our library is initialized, zsv will pass it the address of the zsvlib
 * functions we will be using. We can keep track of this any way we want;
 * in this example, we use a global variable to store the function pointers
 */
static struct zsv_ext_callbacks zsv_cb;

/**
 * Each command must be implemented as a function with a signature
 * as defined by `zsv_ext_main`. This is basically the same as a main() function,
 * but with an additional preceding zsv_execution_context parameter.
 * Here, we just declare the functions; we fully define them further below
 */
enum zsv_ext_status count_main(zsv_execution_context ctx, int argc, const char *argv[]);
static enum zsv_ext_status echo_main(zsv_execution_context ctx, int argc, const char *argv[]);

/**
 * *Required*. Initialization is called when our extension is loaded. Our
 * initialization routine uses `ext_add_command` to register our commands and
 * `ext_set_help` to set the help text. When we register a command, we provide a
 * callback-- in our cases, those will be `count_main()` and `echo_main()`-- for
 * zsv to invoke when a user runs our command
 *
 * @param callbacks pointers to zsvlib functions that we must save for later use
 * @param ctx context to be passed whenever we execute a zsvlib function from our init
 * @return status code e.g. zsv_ext_status_ok
 */

enum zsv_ext_status zsv_ext_init(struct zsv_ext_callbacks *cb, zsv_execution_context ctx) {
  zsv_cb = *cb;
  zsv_cb.ext_set_help(ctx, "Sample zsv extension");
  zsv_cb.ext_set_license(ctx, "Unlicense. See https://github.com/spdx/license-list-data/blob/master/text/Unlicense.txt");
  /**
   * In the common case where your extension uses third-party software, you can add
   * the related licenses and acknowledgements here, which `zsv` will display whenever
   * `zsv thirdparty` is invoked
   */
  const char *third_party_licenses[] = {
    "If we used any third-party software, we would list each license here",
    NULL
  };
  zsv_cb.ext_set_thirdparty(ctx, third_party_licenses);
  zsv_cb.ext_add_command(ctx, "count", "print the number of rows", count_main);
  zsv_cb.ext_add_command(ctx, "echo", "print the input data back to stdout", echo_main);
  return zsv_ext_status_ok;
}

/**
 * exit: called once by zsv before the library is unloaded, if `zsv_ext_init()` was
 * previously called
 */
enum zsv_ext_status zsv_ext_exit() {
  fprintf(stderr, "Exiting dl example!\n");
  return zsv_ext_status_ok;
}
/**
 * Now we are getting to the "meat" of our program. Each command will be defined by two
 * functions:
 *  - a main routine, which initializes private data, calls the parser, then performs any final
 *    steps and/or cleanup
 *  - a row handler, which is called by the parser for each parsed row
 */

/**
 * structure that our functions will use to store private state data
 * which our functions can retrieve using the zsvlib callback `ext_get_context()`
 */
struct my_data {
  zsv_csv_writer csv_writer;
  size_t rows;
};

/**
 * Our row handlers retrieve our state data and update and/or operate on it
 * We can choose, in our main routine, for the ctx pointer it receives to be
 * anything we want, but in most cases there is no reason to set it to anything
 * other than the zsv_execution_context passed to main
 *
 * @param ctx a context pointer of our choice, typically set in the main() function
 */

static void count_rowhandler(void *ctx) {
  /* get our private data */
  struct my_data *data = zsv_cb.ext_get_context(ctx);

  /* increment row counter */
  data->rows++;
}

static void echo_rowhandler(void *ctx) {
  /* get our private data */
  struct my_data *data = zsv_cb.ext_get_context(ctx);

  /**
   * In most cases, we will want to do something with the row that was just parsed,
   * in which case we can use ext_get_parser(), cell_count() and get_cell()
   * to get a parser handle, the number of cells in our current row, and each
   * cell's length and string (and other info)
   */
  zsv_parser parser = zsv_cb.ext_get_parser(ctx);
  unsigned j = zsv_cb.cell_count(parser);
  for(unsigned i = 0; i < j; i++) {
    struct zsv_cell c = zsv_cb.get_cell(parser, i);

     /**
      * get_cell() returns a zsv_cell structure that holds a pointer to the text,
      * the length (in bytes) of the data,
      * and other parser-generated info (e.g. QUOTED flags)
      */
    /* write the cell contents to csv output */
    zsv_writer_cell(data->csv_writer, i == 0, c.str, c.len, c.quoted);
  }
}

/**
 * Our main routines are similar to normal main() functions, except that the first
 * param is a zsvlib-generated zsv_execution_context pointer for use with other
 * `ext_xxx` functions. All we do here is initialize our data, call the parser, and
 * perform any final steps after all data has been processed
 */
static enum zsv_ext_status echo_main(zsv_execution_context ctx, int argc, const char *argv[]) {
  (void)(argc);
  (void)(argv);
  /* initialize private data */
  struct my_data data;
  memset(&data, 0, sizeof(data));

  /* initialize a csv writer, which we add to our private data structure */
  unsigned char writer_buff[128];
  /**
   * the the zsv_writer utility has two special optimizations: first, it
   * uses its own buffer to short-circuit costly stdio operations, and second,
   * it accepts a 'quoted' flag that, if zero, tells it to not bother
   * checking for quote requirements and to just output the contents 'raw'
   */
  if(!(data.csv_writer = zsv_writer_new(NULL)))
    return zsv_ext_status_memory;
  zsv_writer_set_temp_buff(data.csv_writer, writer_buff, sizeof(writer_buff));

  /**
   * The following common parser options that are already handled by zsv (so long as we use
   * either of the provided callbacks `ext_parse_all()` or `new_with_context()`), including:
   * ```
   *   -B,--buff-size <N>
   *   -c,--max-column-count <N>
   *   -r,--max-row-size <N>
   *   -t,--tab-delim
   *   -O,--other-delim <C>
   *   -q,--no-quote
   *   -R,--skip-head <n>: skip specified number of initial rows
   *   -d,--header-row-span <n>: apply header depth (rowspan) of n
   *   -S,--keep-blank-headers: disable default behavior of ignoring leading blank rows
   *   -v,--verbose: verbose output
   * ```
   *
   * If we wanted our command to support parameters for modifying other
   * `zsv_opts`, we could do so here by processing argc and argv[] and taking
   * the appropriate actions to change our private data and/or modify our opts
   * structure
   *
   * For this example, we will not implement any additional options, so the only
   * `zsv_opts` value we need to update is the row handler
   */

  /**
   * use the `ext_parse_all()` convenience function
   */
  struct zsv_opts opts = zsv_cb.ext_parser_opts(ctx);
  enum zsv_ext_status stat = zsv_cb.ext_parse_all(ctx, &data, echo_rowhandler, &opts);

  /**
   * clean up after we finish parsing
   */
  zsv_writer_delete(data.csv_writer);

  /**
   * the return value of the main function will be passed on as the return value
   * of this process invocation
   */
  return stat;
}

/**
 * Our main routine for counting is just like the one for echo, but even simpler
 * since we need not bother with a csv writer
 */
static const char *count_help =
  "count: print the number of rows in a CSV data file\n"
  "\n"
  "usage: count [-h,--help] [filename]\n"
  ;

enum zsv_ext_status count_main(zsv_execution_context ctx, int argc, const char *argv[]) {
  /* help */
  if(argc > 1 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))) {
    printf("%s", count_help);
    return zsv_ext_status_ok;
  }

  /* initialize private data. see above for details */
  struct my_data data = { 0 };
  struct zsv_opts opts = zsv_cb.ext_parser_opts(ctx);

  if(argc > 1 && !(opts.stream = fopen(argv[1], "rb"))) {
    fprintf(stderr, "Unable to open for reading: %s\n", argv[1]);
    return 1;
  }

  /* parse the input data. see above for details */
  enum zsv_ext_status stat = zsv_cb.ext_parse_all(ctx, &data, count_rowhandler, &opts);

  /* finish up */
  if(stat == zsv_ext_status_ok)
    printf("Rows: %zu\n", data.rows > 0 ? data.rows - 1 : 0);

  return stat;
}
