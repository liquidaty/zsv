/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_ARG_H
#define ZSV_ARG_H

#include <zsv/common.h>

/* havearg(): case-insensitive partial arg matching */
char havearg(const char *arg,
             const char *form1, size_t min_len1,
             const char *form2, size_t min_len2);
#endif
