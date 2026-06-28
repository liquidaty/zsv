/*
 * Copyright (C) 2021 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 *
 * `<prog> help toon` / `<prog> toon`: the self-describing TOON reference (R8).
 * Small and stable by design: an agent loads this once to parse TOON reliably.
 */

#include <stdio.h>
#include <zsv/utils/toon.h>
#include <json2toon.h>

static int main_toon(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  static const char *const lines[] = {
    ZSV_USAGE_PROG " toon: TOON (Token-Oriented Object Notation)",
    "",
    "TOON is a compact, lossless, indentation-based encoding of the JSON data",
    "model. Anywhere " ZSV_USAGE_PROG " emits JSON it can emit the identical value",
    "as TOON instead, trading JSON's structural punctuation for fewer tokens.",
    "Decoding TOON yields a value equal to the original JSON (object key order",
    "excepted, as in JSON). Scalar types are preserved exactly: a JSON string",
    "that looks like a number (e.g. \"1000\") stays a string; real numbers and",
    "booleans stay numbers and booleans.",
    "",
    "Naming rule (derivable, no lookup table): the TOON name of any surface is",
    "the JSON name with the substring `json` replaced by `toon`:",
    "  --json        -> --toon            (mutually exclusive with --json)",
    "  2json         -> 2toon",
    "  toon2json     <- (decoder; inverse of 2toon)",
    "  --json-object -> --toon-object",
    "  help X-json   -> help X-toon",
    "",
    "Grammar:",
    "  * Scalars: null, true, false, numbers verbatim; strings bare unless they",
    "    would be ambiguous, in which case they are double-quoted with JSON",
    "    escaping. A bare token equal to null/true/false or matching the JSON",
    "    number grammar decodes to that type; anything else is a string.",
    "  * Objects: one `key: value` per line; nesting by indentation. An empty",
    "    object is `key: {}`.",
    "  * Uniform arrays of objects (every element same keys, all scalar values)",
    "    use the tabular form, which amortizes the field names across all rows:",
    "        key[N]{f1,f2,...}:",
    "          v1,v2,...        (one comma-delimited row per element, N rows)",
    "  * Arrays of scalars use the inline form:  key[N]: v1,v2,...",
    "  * Any other (non-uniform / nested) array uses the list form:",
    "        key[N]:",
    "          - element",
    "          - element",
    "    An empty array is `key: []`.",
    "  * Within tabular rows and inline arrays, a comma inside a string value is",
    "    protected by quoting the value.",
    "",
    "Session default (set-once / set-zero), highest precedence first:",
    "  1. explicit --json / --toon on the command",
    "  2. ZSV_STRUCTURED_FORMAT = json | toon",
    "  3. agent profile: ZSV_AGENT_DEFAULTS = auto|on|off (default auto). When",
    "     auto, the profile is enabled iff AI_AGENT is set and non-blank, and",
    "     defaults structured output to TOON.",
    "  4. built-in default: json",
    "",
    "When (and only when) the agent profile selects TOON, one terse line is",
    "printed to stderr before output:",
    "  " ZSV_USAGE_PROG ": TOON output (AI_AGENT); override: --json; silence: ZSV_AGENT_NOTICE=off",
    "Suppress it with ZSV_AGENT_NOTICE=off (or -q/--quiet where supported).",
    "",
    "Examples:",
    "  " ZSV_USAGE_PROG " 2toon data.csv                 # CSV -> TOON",
    "  " ZSV_USAGE_PROG " 2toon --object data.csv        # TOON tabular array of objects",
    "  " ZSV_USAGE_PROG " toon2json data.toon            # TOON -> JSON (round-trip)",
    "  ZSV_STRUCTURED_FORMAT=toon " ZSV_USAGE_PROG " compare a.csv b.csv --json",
    NULL,
  };
  zsv_print_usage(lines);
  fprintf(stdout, "\nReference encoder/decoder: libjson2toon %s\n", json2toon_version());
  return 0;
}
