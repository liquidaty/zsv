/*
 * Copyright (C) 2023 Liquidaty and the zsv/lib contributors
 * All rights reserved
 *
 * This file is part of zsv/lib, distributed under the license defined at
 * https://opensource.org/licenses/MIT
 */

/*
 * Help-topic printers for `zsv help compare <topic>` (the --json-redline schema
 * and its narrative). #included directly into compare.c as a single translation unit.
 */

void zsv_compare_print_help_topic_narrative(FILE *out) {
  static const char *text[] = {"zsv compare --json-redline schema (version " ZSV_COMPARE_REDLINE_VERSION ")",
                               "==========================================================",
                               "",
                               "Output mode: zsv compare --json-redline [options] <file.csv>...",
                               "",
                               "Produces a single JSON object describing the comparison between two or more",
                               "CSV inputs. A downstream renderer can produce an HTML or XLSX redline from",
                               "this output without re-reading the source CSVs.",
                               "",
                               "Top-level fields",
                               "  schema        : \"zsv.compare\" (constant)",
                               "  version       : \"" ZSV_COMPARE_REDLINE_VERSION "\" — bump on breaking changes only",
                               "  generated_at  : ISO 8601 UTC timestamp (honors SOURCE_DATE_EPOCH)",
                               "  inputs[]      : per-input metadata (label, path, row_count)",
                               "  keys[]        : column names used to match rows",
                               "  options       : tolerance, sort, include_unchanged_rows, include_tolerated,",
                               "                  require_all_inputs",
                               "  columns[]     : merged column list with is_key and in_inputs[]",
                               "  summary       : aggregate row/cell counts and schema diff",
                               "  rows[]        : differing (and optionally unchanged) rows",
                               "",
                               "Row forms",
                               "  Array form    : row present in every input — [cell_0, cell_1, ...]",
                               "  Object form   : row missing in some input —",
                               "                  {\"data\": [...], \"missing_in\": [<input indices>]}",
                               "                  missing_in indices are in ascending order",
                               "",
                               "Cell forms",
                               "  Scalar        : string | null — all inputs agree (or tolerated diff collapsed)",
                               "  Array         : [v_0, v_1, ..., v_N] parallel to inputs[] — values differ",
                               "                  null slot means that input lacks this cell",
                               "",
                               "Tolerance",
                               "  With --tolerance T, numeric cells differing by < T render as scalars",
                               "  (input[0]'s value) unless --include-tolerated is also set.",
                               "",
                               "See also",
                               "  zsv help compare json-redline-schema  (JSON Schema Draft 2020-12)",
                               NULL};
  for (size_t i = 0; text[i]; i++)
    fprintf(out, "%s\n", text[i]);
}

void zsv_compare_print_help_topic_json_schema(FILE *out) {
  fprintf(out, "{\n");
  fprintf(out, "  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n");
  fprintf(out, "  \"$id\": \"zsv.compare.json-redline.v%s\",\n", ZSV_COMPARE_REDLINE_VERSION);
  fprintf(out, "  \"title\": \"zsv compare --json-redline output (v%s)\",\n", ZSV_COMPARE_REDLINE_VERSION);
  fprintf(out, "  \"type\": \"object\",\n");
  fprintf(
    out,
    "  \"required\": "
    "[\"schema\",\"version\",\"generated_at\",\"inputs\",\"keys\",\"options\",\"columns\",\"summary\",\"rows\"],\n");
  fprintf(out, "  \"properties\": {\n");
  fprintf(out, "    \"schema\": {\"const\": \"zsv.compare\"},\n");
  fprintf(out, "    \"version\": {\"const\": \"%s\"},\n", ZSV_COMPARE_REDLINE_VERSION);
  fprintf(out, "    \"generated_at\": {\"type\": \"string\"},\n");
  fprintf(out, "    \"inputs\": {\n");
  fprintf(out, "      \"type\": \"array\",\n");
  fprintf(out, "      \"items\": {\n");
  fprintf(out, "        \"type\": \"object\",\n");
  fprintf(out, "        \"required\": [\"label\",\"path\",\"row_count\"],\n");
  fprintf(out, "        \"properties\": {\n");
  fprintf(out, "          \"label\": {\"type\": \"string\"},\n");
  fprintf(out, "          \"path\": {\"type\": \"string\"},\n");
  fprintf(out, "          \"row_count\": {\"type\": \"integer\", \"minimum\": 0}\n");
  fprintf(out, "        }\n");
  fprintf(out, "      }\n");
  fprintf(out, "    },\n");
  fprintf(out, "    \"keys\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n");
  fprintf(out, "    \"options\": {\n");
  fprintf(out, "      \"type\": \"object\",\n");
  fprintf(out, "      \"required\": [\"tolerance\",\"sort\",\"include_unchanged_rows\",\"include_tolerated\"],\n");
  fprintf(out, "      \"properties\": {\n");
  fprintf(out, "        \"tolerance\": {\"oneOf\": [{\"type\": \"number\"},{\"type\": \"null\"}]},\n");
  fprintf(out, "        \"sort\": {\"type\": \"boolean\"},\n");
  fprintf(out, "        \"include_unchanged_rows\": {\"type\": \"boolean\"},\n");
  fprintf(out, "        \"include_tolerated\": {\"type\": \"boolean\"},\n");
  fprintf(out, "        \"require_all_inputs\": {\"type\": \"boolean\"}\n");
  fprintf(out, "      }\n");
  fprintf(out, "    },\n");
  fprintf(out, "    \"columns\": {\n");
  fprintf(out, "      \"type\": \"array\",\n");
  fprintf(out, "      \"items\": {\n");
  fprintf(out, "        \"type\": \"object\",\n");
  fprintf(out, "        \"required\": [\"name\",\"is_key\",\"in_inputs\"],\n");
  fprintf(out, "        \"properties\": {\n");
  fprintf(out, "          \"name\": {\"type\": \"string\"},\n");
  fprintf(out, "          \"is_key\": {\"type\": \"boolean\"},\n");
  fprintf(out, "          \"in_inputs\": {\"type\": \"array\", \"items\": {\"type\": \"integer\", \"minimum\": 0}}\n");
  fprintf(out, "        }\n");
  fprintf(out, "      }\n");
  fprintf(out, "    },\n");
  fprintf(out, "    \"summary\": {\n");
  fprintf(out, "      \"type\": \"object\",\n");
  fprintf(out, "      \"required\": [\"rows\",\"cells\",\"by_column\",\"schema\"],\n");
  fprintf(out, "      \"properties\": {\n");
  fprintf(out, "        \"rows\": {\n");
  fprintf(out, "          \"type\": \"object\",\n");
  fprintf(out, "          \"required\": [\"in_all_inputs\",\"only_in_input_count\",\"with_any_diff\"],\n");
  fprintf(out, "          \"properties\": {\n");
  fprintf(out, "            \"in_all_inputs\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out,
          "            \"only_in_input_count\": {\"type\": \"array\", \"items\": {\"type\": \"integer\", \"minimum\": "
          "0}},\n");
  fprintf(out, "            \"with_any_diff\": {\"type\": \"integer\", \"minimum\": 0}\n");
  fprintf(out, "          }\n");
  fprintf(out, "        },\n");
  fprintf(out, "        \"cells\": {\n");
  fprintf(out, "          \"type\": \"object\",\n");
  fprintf(out, "          \"required\": [\"compared\",\"matched\",\"within_tolerance\",\"differing\"],\n");
  fprintf(out, "          \"properties\": {\n");
  fprintf(out, "            \"compared\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out, "            \"matched\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out, "            \"within_tolerance\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out, "            \"differing\": {\"type\": \"integer\", \"minimum\": 0}\n");
  fprintf(out, "          }\n");
  fprintf(out, "        },\n");
  fprintf(out, "        \"by_column\": {\n");
  fprintf(out, "          \"type\": \"array\",\n");
  fprintf(out, "          \"items\": {\n");
  fprintf(out, "            \"type\": \"object\",\n");
  fprintf(out, "            \"required\": [\"name\",\"compared\",\"matched\",\"within_tolerance\",\"differing\"],\n");
  fprintf(out, "            \"properties\": {\n");
  fprintf(out, "              \"name\": {\"type\": \"string\"},\n");
  fprintf(out, "              \"compared\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out, "              \"matched\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out, "              \"within_tolerance\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out, "              \"differing\": {\"type\": \"integer\", \"minimum\": 0}\n");
  fprintf(out, "            }\n");
  fprintf(out, "          }\n");
  fprintf(out, "        },\n");
  fprintf(out, "        \"schema\": {\n");
  fprintf(out, "          \"type\": \"object\",\n");
  fprintf(out, "          \"required\": [\"common\",\"only_in_input\"],\n");
  fprintf(out, "          \"properties\": {\n");
  fprintf(out, "            \"common\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n");
  fprintf(out,
          "            \"only_in_input\": {\"type\": \"array\", \"items\": {\"type\": \"array\", \"items\": {\"type\": "
          "\"string\"}}}\n");
  fprintf(out, "          }\n");
  fprintf(out, "        }\n");
  fprintf(out, "      }\n");
  fprintf(out, "    },\n");
  fprintf(out, "    \"rows\": {\n");
  fprintf(out, "      \"type\": \"array\",\n");
  fprintf(out, "      \"items\": {\n");
  fprintf(out, "        \"oneOf\": [\n");
  fprintf(out, "          {\n");
  fprintf(out, "            \"type\": \"array\",\n");
  fprintf(out, "            \"items\": {\n");
  fprintf(out, "              \"oneOf\": [\n");
  fprintf(out, "                {\"type\": [\"string\",\"null\"]},\n");
  fprintf(out, "                {\"type\": \"array\", \"items\": {\"type\": [\"string\",\"null\"]}}\n");
  fprintf(out, "              ]\n");
  fprintf(out, "            }\n");
  fprintf(out, "          },\n");
  fprintf(out, "          {\n");
  fprintf(out, "            \"type\": \"object\",\n");
  fprintf(out, "            \"required\": [\"data\",\"missing_in\"],\n");
  fprintf(out, "            \"properties\": {\n");
  fprintf(out, "              \"data\": {\n");
  fprintf(out, "                \"type\": \"array\",\n");
  fprintf(out, "                \"items\": {\n");
  fprintf(out, "                  \"oneOf\": [\n");
  fprintf(out, "                    {\"type\": [\"string\",\"null\"]},\n");
  fprintf(out, "                    {\"type\": \"array\", \"items\": {\"type\": [\"string\",\"null\"]}}\n");
  fprintf(out, "                  ]\n");
  fprintf(out, "                }\n");
  fprintf(out, "              },\n");
  fprintf(out, "              \"missing_in\": {\n");
  fprintf(out, "                \"type\": \"array\",\n");
  fprintf(out, "                \"items\": {\"type\": \"integer\", \"minimum\": 0},\n");
  fprintf(out, "                \"description\": \"indices in ascending order\"\n");
  fprintf(out, "              }\n");
  fprintf(out, "            }\n");
  fprintf(out, "          }\n");
  fprintf(out, "        ]\n");
  fprintf(out, "      }\n");
  fprintf(out, "    }\n");
  fprintf(out, "  }\n");
  fprintf(out, "}\n");
}

static int zsv_compare_print_help_topic(const char *name) {
  /* canonical: json-redline, json-redline-schema
     silent aliases (undocumented): compare-json-redline, json-redline-json, compare-json-redline-schema
  */
  if (!strcmp(name, "json-redline") || !strcmp(name, "compare-json-redline")) {
    zsv_compare_print_help_topic_narrative(stdout);
    return 0;
  }
  if (!strcmp(name, "json-redline-schema") || !strcmp(name, "json-redline-json") ||
      !strcmp(name, "compare-json-redline-schema")) {
    zsv_compare_print_help_topic_json_schema(stdout);
    return 0;
  }
  fprintf(stderr, "Unknown help topic: %s\n", name);
  fprintf(stderr, "Available topics: json-redline, json-redline-schema\n");
  return 1;
}
