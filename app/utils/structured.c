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

#include <zsv/utils/structured.h>
#include <zsv/utils/appname.h>

/* Return the (non-empty, leading-whitespace-trimmed) value of env var `name`,
 * or NULL. */
static const char *env_get(const char *name) {
  const char *v = getenv(name);
  if (!v)
    return NULL;
  while (*v == ' ' || *v == '\t')
    v++;
  if (!*v)
    return NULL;
  return v;
}

static int str_ci_eq(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z')
      ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z')
      cb = (char)(cb - 'A' + 'a');
    if (ca != cb)
      return 0;
  }
  return *a == *b;
}

int zsv_structured_resolve(const struct zsv_structured_opts *opts, enum zsv_structured_source *source) {
  enum zsv_structured_source src = zsv_structured_source_default;
  enum zsv_structured_format fmt = zsv_structured_format_json;

  if (opts && opts->flag_json && opts->flag_toon)
    return -1;

  if (opts && (opts->flag_json || opts->flag_toon)) {
    fmt = opts->flag_toon ? zsv_structured_format_toon : zsv_structured_format_json;
    src = zsv_structured_source_flag;
  } else {
    const char *envfmt = env_get("ZSV_STRUCTURED_FORMAT");
    if (envfmt && (str_ci_eq(envfmt, "toon") || str_ci_eq(envfmt, "json"))) {
      fmt = str_ci_eq(envfmt, "toon") ? zsv_structured_format_toon : zsv_structured_format_json;
      src = zsv_structured_source_env;
    } else {
      /* agent profile */
      const char *defaults = env_get("ZSV_AGENT_DEFAULTS");
      int profile_on;
      if (defaults && str_ci_eq(defaults, "on"))
        profile_on = 1;
      else if (defaults && str_ci_eq(defaults, "off"))
        profile_on = 0;
      else /* "auto" (default): enabled iff AI_AGENT is set and non-blank */
        profile_on = env_get("AI_AGENT") != NULL;
      if (profile_on) {
        fmt = zsv_structured_format_toon;
        src = zsv_structured_source_agent;
      }
    }
  }

  if (source)
    *source = src;
  return (int)fmt;
}

void zsv_structured_emit_notice(enum zsv_structured_format fmt, enum zsv_structured_source source, int quiet) {
  static int emitted = 0;
  if (source != zsv_structured_source_agent)
    return;
  if (fmt != zsv_structured_format_toon)
    return;
  if (quiet || emitted)
    return;
  const char *notice = getenv("ZSV_AGENT_NOTICE");
  if (notice && str_ci_eq(notice, "off"))
    return;
  emitted = 1;
  /* e.g. "<prog>: TOON output (AI_AGENT); override: --json; silence: ZSV_AGENT_NOTICE=off"
   * where <prog> is the runtime program name (zsv_prog_name()), never hardcoded. */
  fprintf(stderr, "%s: TOON output (AI_AGENT); override: --json; silence: ZSV_AGENT_NOTICE=off\n", zsv_prog_name());
}

const char *zsv_structured_describe(enum zsv_structured_format fmt, enum zsv_structured_source source) {
  static char buf[160];
  const char *fmtname = (fmt == zsv_structured_format_toon) ? "toon" : "json";
  const char *srcdesc;
  switch (source) {
  case zsv_structured_source_flag:
    srcdesc = "command-line flag";
    break;
  case zsv_structured_source_env:
    srcdesc = "ZSV_STRUCTURED_FORMAT";
    break;
  case zsv_structured_source_agent:
    srcdesc = "agent profile; AI_AGENT detected";
    break;
  case zsv_structured_source_default:
  default:
    srcdesc = "built-in default";
    break;
  }
  snprintf(buf, sizeof(buf), "structured-output=%s (source: %s)", fmtname, srcdesc);
  return buf;
}
