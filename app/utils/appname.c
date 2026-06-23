/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <zsv/utils/appname.h>
#include <zsv/utils/file.h> /* zsv_dir_len_basename */

static char prog_name[128] = ZSV_DEFAULT_PROG_NAME;

void zsv_set_prog_name(const char *host) {
  if (host && *host) {
    const char *base = host;
    zsv_dir_len_basename(host, &base); /* portable: splits on '/' and '\\' */
    snprintf(prog_name, sizeof(prog_name), "%s", base);
  }
}

const char *zsv_prog_name(void) {
  return prog_name;
}

void zsv_fprint_usage(FILE *f, const char *const *lines) {
  for (; *lines; lines++) {
    for (const char *p = *lines; *p; p++) {
      if (*p == ZSV_USAGE_PROG[0])
        fputs(prog_name, f);
      else
        putc(*p, f);
    }
    putc('\n', f);
  }
}

void zsv_print_usage(const char *const *lines) {
  zsv_fprint_usage(stdout, lines);
}
