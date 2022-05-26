/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

static int main_help(int argc, const char *argv[]) {
  (void)(argc);
  (void)(argv);

  FILE *f = stdout;
  static const char *usage[] = {
    "zsv: streaming csv processor",
    "",
    "Usage:",
    "  zsv version: display version info (and if applicable, extension info)",
    "  zsv (un)register [<extension_id>]: (un)register an extension",
    "  zsv help [<command>]",
    "  zsv license [<extension_id>]",
    "  zsv <command> <options> <arguments>: run a command on data (see below for details)",
    "  zsv <id>-<cmd> <options> <arguments>: invoke command 'cmd' of extension 'id'",
    "",
    "Options common to all commands:",
    "  -c,--max-column-count: set the maximum number of columns parsed per row. defaults to 1024",
    "  -r,--max-row-size: set the minimum supported maximum row size. defaults to 64k",
    "  -B,--buff-size: set internal buffer size. defaults to 256k",
    "  -t,--tab-delim: set column delimiter to tab",
    "  -O,--other-delim: set column delimiter to other column",
    "  -q,--no-quote: turn off quote handling",
    "  -v,--verbose: verbose output",
    "",
    "Commands:",
    "  select: extract rows/columns by name or position and perform other basic and 'cleanup' operations",
    "  sql: run ad-hoc SQL",
    "  count: print the number of rows",
    "  desc: describe each column",
    "  pretty: pretty print for console display",
    "  flatten: flatten a table consisting of N groups of data, each with 1 or",
    "           more rows in the table, into a table of N rows",
    "  2json: convert to json",
    "  2tsv : convert to tab-delimited text",
    "  serialize: convert into 3-column format (id, column name, cell value)",
    "  stack: stack tables vertically, aligning columns with common names",
    // to do: "  2csv: convert to CSV from fixed, delim, html, xml, parquet..."
    NULL
  };

  for(int i = 0; usage[i]; i++)
    fprintf(f, "%s\n", usage[i]);

  char printed_init = 0;
  struct cli_config config;
  if(!config_init(&config, 1, 1, 0)) {
    for(struct zsv_ext *ext = config.extensions; ext; ext = ext->next) {
      if(ext->inited == zsv_init_ok) {
        if(!printed_init) {
          printed_init = 1;
          fprintf(f, "\nExtended commands:\n");
        } else
          fprintf(f, "\n");
        if(ext->help)
          fprintf(f, "  Extension '%s': %s\n", ext->id, ext->help);
        for(struct zsv_ext_command *cmd = ext->commands; cmd; cmd = cmd->next)
          fprintf(f, "    %s-%s%s%s\n", ext->id, cmd->id, cmd->help ? ": " : "", cmd->help ? cmd->help : "");
      }
      config_free(&config);
    }
  }
  if(!printed_init)
    fprintf(f, "\n(No extended commands)\n");

  return 0;
}
