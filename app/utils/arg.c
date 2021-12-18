/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zsv.h>
#include <zsv/utils/string.h>
#include <zsv/utils/arg.h>

/* havearg(): case-insensitive partial arg matching */
char havearg(const char *arg,
             const char *form1, size_t min_len1,
             const char *form2, size_t min_len2) {
  size_t len = strlen(arg);
  if(!min_len1)
    min_len1 = strlen(form1);
  if(len > min_len1)
    min_len1 = len;

  if(!zsv_strincmp((const unsigned char *)arg, min_len1,
                     (const unsigned char *)form1, min_len1))
    return 1;

  if(form2) {
    if(!min_len2)
      min_len2 = strlen(form2);
    if(len > min_len2)
      min_len2 = len;
    if(!zsv_strincmp((const unsigned char *)arg, min_len2,
                       (const unsigned char *)form2, min_len2))
      return 1;
  }
  return 0;
}
