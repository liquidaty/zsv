/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 *
 * Resolution of the structured-output encoding (JSON vs TOON) shared by every
 * command that can emit structured output. Implements the precedence chain and
 * observability rules from the TOON-output spec (U7-U11):
 *
 *   1. explicit per-command --json / --toon flag
 *   2. ZSV_STRUCTURED_FORMAT  (json|toon)
 *   3. agent profile: ZSV_AGENT_DEFAULTS (auto|on|off) + AI_AGENT  => toon
 *   4. built-in default: json
 *
 * All environment variables live in the zsv layer and are therefore ZSV_*. A
 * host program built on zsv may add its own branded aliases in its own layer.
 *
 * Anything explicit always beats the auto profile. The agent profile only
 * changes the *encoding* of already-structured output; it never changes which
 * commands emit structured output, nor CSV/other defaults, nor data semantics.
 */

#ifndef ZSV_STRUCTURED_H
#define ZSV_STRUCTURED_H

enum zsv_structured_format {
  zsv_structured_format_json = 0,
  zsv_structured_format_toon = 1
};

enum zsv_structured_source {
  zsv_structured_source_default = 0, /* built-in default (json) */
  zsv_structured_source_flag,        /* explicit --json / --toon */
  zsv_structured_source_env,         /* ZSV_STRUCTURED_FORMAT */
  zsv_structured_source_agent        /* AI_AGENT auto-profile */
};

struct zsv_structured_opts {
  unsigned char flag_json; /* nonzero if --json was given */
  unsigned char flag_toon; /* nonzero if --toon was given */
  unsigned char quiet;     /* nonzero if -q/--quiet was given */
};

/**
 * Resolve the structured-output encoding per the precedence chain above.
 * If both --json and --toon are set, returns -1 (usage error; caller should
 * emit a one-line message and exit 1). Otherwise sets *source (may be NULL) and
 * returns the resolved format.
 */
int zsv_structured_resolve(const struct zsv_structured_opts *opts, enum zsv_structured_source *source);

/**
 * Print the one-line stderr notice mandated by U11a, but ONLY when the encoding
 * was selected by the agent auto-profile (source == agent), at most once per
 * process, and only if not quiet and ZSV_AGENT_NOTICE != "off". No-op otherwise.
 */
void zsv_structured_emit_notice(enum zsv_structured_format fmt, enum zsv_structured_source source, int quiet);

/**
 * Human-readable description of the resolved encoding and its source, for
 * -v/--verbose and config inspection (U11c). Returns a pointer to a static
 * buffer (overwritten on each call).
 */
const char *zsv_structured_describe(enum zsv_structured_format fmt, enum zsv_structured_source source);

#endif
