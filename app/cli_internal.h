/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <zsv.h>
struct zsv_ext_command {
  struct zsv_ext_command *next;
  char *id;
  char *help;
  zsv_ext_main main;
};

struct zsv_ext {
  struct zsv_ext *next;
  void *dl;
  char *id;
  char ok;
  char *help;
  char *license;
  char *thirdparty;
  struct zsv_ext_command *commands;
  struct zsv_ext_command **commands_next;
#define zsv_init_started 1
#define zsv_init_ok 2
  char inited;
  struct {
# include "cli_internal.h.in"
  } module;
};

static int zsv_unload_custom_cmds(struct zsv_ext *ext);
static enum zsv_ext_status zsv_ext_delete(struct zsv_ext *ext);
