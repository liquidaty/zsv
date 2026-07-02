/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

#ifndef ZSV_OUTPUT_H
#define ZSV_OUTPUT_H

#include <stdlib.h>

enum zsv_output_format {
  zsv_output_format_json = 0,
  zsv_output_format_toon = 1
};

/*
 * get_default_output_format(): the output format a command should use when the
 * caller has not explicitly selected one. Returns TOON when the AI_AGENT
 * environment variable is set to a non-blank value (so agents get token-dense
 * TOON by default), otherwise JSON. An explicit command-line format option
 * always overrides this.
 */
static inline enum zsv_output_format get_default_output_format(void) {
  const char *e = getenv("AI_AGENT");
  if (e)
    while (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')
      e++;
  return e && *e ? zsv_output_format_toon : zsv_output_format_json;
}

#endif
