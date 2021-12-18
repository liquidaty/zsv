/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdarg.h>

int zsv_app_printerr(const char *appname, int eno, const char *fmt, ... ) {
  if(appname && *appname)
    fprintf(stderr, "(%s) %i: ", appname, eno);
  else
    fprintf(stderr, "%i: ", eno);

  va_list argv;
  va_start(argv, fmt);
  vfprintf(stderr, fmt, argv);
  va_end(argv);
  fprintf(stderr, "\n");
  return eno;
}
