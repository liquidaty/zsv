/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>

static int main_help(int argc, const char *argv[]) {
  (void)(argc);
  (void)(argv);

  FILE *f = stdout;

#include "help_usage.h"

  zsv_fprint_usage(f, usage);
  fprintf(f, "\n%s\n", common_options_title);
  for (size_t i = 0; common_options[i]; i++)
    fprintf(f, "%s\n", common_options[i]);
  fprintf(f, "\n%s\n", commands_title);
  for (size_t i = 0; commands[i].name || commands[i].synopsis; i++) {
    if (!commands[i].name) /* section break: print synopsis as a sub-heading */
      fprintf(f, "\n%s\n", commands[i].synopsis);
    else
      fprintf(f, "  %-9s: %s\n", commands[i].name, commands[i].synopsis);
  }

#ifndef __EMSCRIPTEN__
  char printed_init = 0;
  struct cli_config config;
  if (!config_init(&config, 1, 1, 0)) {
    for (struct zsv_ext *ext = config.extensions; ext; ext = ext->next) {
      if (ext->inited == zsv_init_ok) {
        if (!printed_init) {
          printed_init = 1;
          fprintf(f, "\nExtended commands:\n");
        } else
          fprintf(f, "\n");
        if (ext->help)
          fprintf(f, "  Extension '%s': %s\n", ext->id, ext->help);
        for (struct zsv_ext_command *cmd = ext->commands; cmd; cmd = cmd->next)
          fprintf(f, "    %s-%s%s%s\n", ext->id, cmd->id, cmd->help ? ": " : "", cmd->help ? cmd->help : "");
      }
    }
    config_free(&config);
  }
  if (!printed_init)
    fprintf(f, "\n(No extended commands)\n");
#endif

  fprintf(f, "\nTo learn more, see README at https://github.com/liquidaty/zsv.\n");
  fprintf(f, "Report any issues at https://github.com/liquidaty/zsv/issues.\n");

  return 0;
}
