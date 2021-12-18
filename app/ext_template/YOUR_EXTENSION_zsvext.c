/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zsv/ext/implementation.h>

/**
 * `zsv` can easily be extended by simply creating a shared library
 * that implements the interface specified in zsv/ext/implementation.h
 *
 * This file is a template you can use to implement your own extension.
 * All you need to do is customize the sections marked with the word
 *   `YOUR`
 *
 * e.g. `YOUR_COMMAND` or `... YOUR CODE GOES HERE ...`
 *
 * replace any occurrences of "YOUR_COMMAND" with your (first) command, and
 * dupe any occurrences of "YOUR_COMMAND" for any additional commands
 */

/**
 * Define our extension ID. You canm make this anything you want, so long
 * as it is comprised of two ascii characters. In the rest of this file
 * commentary, XX refers to this extension ID
 */
#define ZSV_THIS_EXT_ID "xx" /* YOUR EXTENSION ID GOES HERE */

/**
 * Once the library file is created, you can run any commands it implements
 * by naming the library file zsvext<id>, placing it in any folder that is
 * in the system lib search path, or in the same folder as zsv, and running:
 *    `zsv XX-command`
 *
 * It's as simple as that!
 */

/**
 * Below implements the following command(s):
 * * `YOUR_COMMAND`: your first command description
 * * [ ... `zsv XX-YOUR_COMMAND2` ]
 * * [ ... ]
 *
 *
 * in addition, users will see a brief description of our module if they execute:
 *   `zsv help` or `zsv help XX-<command>`
 */

/**
 * *Required*: define our extension id, which must be two characters in length
 */
const char *zsv_ext_id() {
  return ZSV_THIS_EXT_ID;
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
static enum zsv_ext_status YOUR_COMMAND_main(
  zsv_execution_context ctx, int argc, const char *argv[]
);

/*
  static enum zsv_ext_status YOUR_COMMAND2_main(...);
  static enum zsv_ext_status YOUR_COMMAND3_main(...);
  ...
*/

/**
 * *Required*. Initialization is called when our extension is loaded. Our
 * initialization routine uses `ext_add_command` to register our commands and
 * `ext_set_help` to set the help text. For each registerd command, we provide a
 * `*_main()` callback for zsv to invoke when a user runs our command
 *
 * @param callbacks pointers to zsvlib functions that we must save for later use
 * @param ctx context to be passed whenever we execute a zsvlib function from our init
 * @return status code e.g. zsv_ext_status_ok
 */
enum zsv_ext_status zsv_ext_init(struct zsv_ext_callbacks *cb, zsv_execution_context ctx) {
  zsv_cb = *cb;
  zsv_cb.ext_set_help(ctx, "YOUR brief help message goes here");
  zsv_cb.ext_set_license(ctx, "YOUR license text goes here");

  /**
   * In the common case where your extension uses third-party software, you can add
   * the related licenses and acknowledgements here, which `zsv` will display whenever
   * `zsv thirdparty` is invoked
   */
  static const char *third_party_licenses[] = {
    "YOUR third-party licenses & acknowledgements go here",
    NULL
  };
  zsv_cb.ext_set_thirdparty(ctx, third_party_licenses);
  zsv_cb.ext_add_command(ctx, "YOUR_COMMAND", "YOUR command description", YOUR_COMMAND_main);

  /* YOUR CODE GOES HERE if you will perform any one-time initialization / allocation */
  return zsv_ext_status_ok;
}

/**
 * If you allocated resources in `zsv_ext_init()`
 * then you can de-allocate them in `zsv_ext_exit()`
 */
enum zsv_ext_status zsv_ext_exit() {
  /* YOUR CODE GOES HERE */
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
 * Define a structure for our functions to store private state data
 * which our functions can retrieve using the zsvlib callback `ext_get_context()`
 */
struct xx_data {
  size_t rows; /* Replace this line with YOUR data fields */
};

/**
 * Callback to initialize our private data
 */
static void xx_data_init(struct xx_data *data) {
  /* YOUR CODE GOES HERE */
  memset(data, 0, sizeof(*data));
}

/**
 * Callback to clean up our private data
 */
static void xx_data_free(struct xx_data *data) {
  /* YOUR CODE GOES HERE */
  (void)(data);
}

/**
 * Our row handlers retrieve our state data and update and/or operate on it
 * We can choose, in our main routine, for the ctx pointer it receives to be
 * anything we want, but in most cases there is no reason to set it to anything
 * other than the zsv_execution_context passed to main
 *
 * @param ctx a context pointer of our choice, typically set in the main() function
 */

static void YOUR_COMMAND_rowhandler(void *ctx) {
  /* get our private data */
  struct xx_data *data = zsv_cb.ext_get_context(ctx);
  data->rows++; /* replace this line with YOUR CODE */
  
  /**
   * In most cases, we will want to do something with the row that was just parsed,
   * in which case we can use ext_get_parser(), column_count() and get_cell()
   * to get a parser handle, the number of cells in our current row, and each
   * cell's length and string (and other info)
   */
  zsv_parser parser = zsv_cb.ext_get_parser(ctx);
  unsigned cell_count = zsv_cb.column_count(parser);
  for(unsigned i = 0; i < cell_count; i++) {
    /**
     * get_cell() returns a zsv_cell structure that holds a pointer to the text,
     * the length (in bytes) of the data,
     * and other parser-generated info (e.g. QUOTED flags)
     */
    struct zsv_cell c = zsv_cb.get_cell(parser, i);
    /* YOUR CODE HERE to process each cell e.g. `fwrite(c.str, 1, c.len, stdout)` */
  }
}

/**
 * Our main routines are similar to normal main() functions, except that the first
 * param is a zsvlib-generated zsv_execution_context pointer for use with other
 * `ext_xxx` functions. All we do here is initialize our data, call the parser, and
 * perform any final steps after all data has been processed
 */
static enum zsv_ext_status YOUR_COMMAND_main(zsv_execution_context ctx, int argc, const char *argv[]) {
  /* initialize private data */
  struct xx_data data;
  xx_data_init(&data);

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
   *   -v,--verbose
   * ```
   *
   * If we wanted our command to support parameters for modifying other
   * `zsv_opts`, we could do so here by processing argc and argv[] and taking
   * the appropriate actions to change our private data and/or modify our opts
   * structure
   */

  /**
   * If your process needs only to read a single input table sequentially from
   * a FILE pointer, you can simplify your code by using `ext_parse_all()`,
   * a convenience function that:
   * 1. binds our custom context (in this case, a pointer to our `xx_data`
   *    structure) to the execution context, so that our row handler can
   *    retrieve it using `ext_get_context()`
   * 2. parses the specified input (in our case, the default which is stdin) and
   *    binds the parser to the execution context, so that our row handler can
   *    retrieve it (and the current row's data) using `ext_get_parser()`
   * 3. cleans up / deallocates the parser and related resources
   */
  enum zsv_ext_status stat = zsv_cb.ext_parse_all(ctx, &data, YOUR_COMMAND_rowhandler, NULL);

  /**
   * Alternatively, for more granular control we could use the following:
   * ```
   * struct zsv_opts opts;
   * memset(&opts, 0, sizeof(opts));
   * enum zsv_ext_status stat = zsv_cb.ext_parser_opts(ctx, &opts);
   * if(stat == zsv_ext_status_ok) {
   *   zsv_parser parser = new_with_context(ctx, &opts);
   *   if(!parser)
   *     stat = zsv_ext_status_memory;
   *   else {
   *     opts.row = YOUR_COMMAND_rowhandler;
   *     // ... set other options here ...
   *     zsv_parser p = new_with_context(ctx, &opts);
   *     while((stat = zsv_parse_more(parser)) == zsv_status_ok) ;
   *     if(stat == zsv_status_ok)
   *       stat = zsv_finish(p);
   *     zsv_delete(p);
   *   }
   * }
   * ```
   */

  /* done parsing */
  if(stat == zsv_ext_status_ok) {
    /* Successful run. Replace the below line with YOUR CODE */
    printf("Rows: %zu\n", data.rows > 0 ? data.rows - 1 : 0);
  }

  /* clean up */
  xx_data_free(&data);

  /**
   * the return value of the main function will be passed on as the return value
   * of this process invocation
   */
  return stat;
}

#undef ZSV_THIS_EXT_ID
