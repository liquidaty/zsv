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

static void zsv_compare_print_help_topic_narrative(void) {
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
                               "  options       : tolerance, sort, include_unchanged_rows, include_tolerated",
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
    printf("%s\n", text[i]);
}

static void zsv_compare_print_help_topic_json_schema(void) {
  printf("{\n");
  printf("  \"$schema\": \"https://json-schema.org/draft/2020-12/schema\",\n");
  printf("  \"$id\": \"zsv.compare.json-redline.v%s\",\n", ZSV_COMPARE_REDLINE_VERSION);
  printf("  \"title\": \"zsv compare --json-redline output (v%s)\",\n", ZSV_COMPARE_REDLINE_VERSION);
  printf("  \"type\": \"object\",\n");
  printf(
    "  \"required\": "
    "[\"schema\",\"version\",\"generated_at\",\"inputs\",\"keys\",\"options\",\"columns\",\"summary\",\"rows\"],\n");
  printf("  \"properties\": {\n");
  printf("    \"schema\": {\"const\": \"zsv.compare\"},\n");
  printf("    \"version\": {\"const\": \"%s\"},\n", ZSV_COMPARE_REDLINE_VERSION);
  printf("    \"generated_at\": {\"type\": \"string\"},\n");
  printf("    \"inputs\": {\n");
  printf("      \"type\": \"array\",\n");
  printf("      \"items\": {\n");
  printf("        \"type\": \"object\",\n");
  printf("        \"required\": [\"label\",\"path\",\"row_count\"],\n");
  printf("        \"properties\": {\n");
  printf("          \"label\": {\"type\": \"string\"},\n");
  printf("          \"path\": {\"type\": \"string\"},\n");
  printf("          \"row_count\": {\"type\": \"integer\", \"minimum\": 0}\n");
  printf("        }\n");
  printf("      }\n");
  printf("    },\n");
  printf("    \"keys\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n");
  printf("    \"options\": {\n");
  printf("      \"type\": \"object\",\n");
  printf("      \"required\": [\"tolerance\",\"sort\",\"include_unchanged_rows\",\"include_tolerated\"],\n");
  printf("      \"properties\": {\n");
  printf("        \"tolerance\": {\"oneOf\": [{\"type\": \"number\"},{\"type\": \"null\"}]},\n");
  printf("        \"sort\": {\"type\": \"boolean\"},\n");
  printf("        \"include_unchanged_rows\": {\"type\": \"boolean\"},\n");
  printf("        \"include_tolerated\": {\"type\": \"boolean\"}\n");
  printf("      }\n");
  printf("    },\n");
  printf("    \"columns\": {\n");
  printf("      \"type\": \"array\",\n");
  printf("      \"items\": {\n");
  printf("        \"type\": \"object\",\n");
  printf("        \"required\": [\"name\",\"is_key\",\"in_inputs\"],\n");
  printf("        \"properties\": {\n");
  printf("          \"name\": {\"type\": \"string\"},\n");
  printf("          \"is_key\": {\"type\": \"boolean\"},\n");
  printf("          \"in_inputs\": {\"type\": \"array\", \"items\": {\"type\": \"integer\", \"minimum\": 0}}\n");
  printf("        }\n");
  printf("      }\n");
  printf("    },\n");
  printf("    \"summary\": {\n");
  printf("      \"type\": \"object\",\n");
  printf("      \"required\": [\"rows\",\"cells\",\"by_column\",\"schema\"],\n");
  printf("      \"properties\": {\n");
  printf("        \"rows\": {\n");
  printf("          \"type\": \"object\",\n");
  printf("          \"required\": [\"in_all_inputs\",\"only_in_input_count\",\"with_any_diff\"],\n");
  printf("          \"properties\": {\n");
  printf("            \"in_all_inputs\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("            \"only_in_input_count\": {\"type\": \"array\", \"items\": {\"type\": \"integer\", \"minimum\": "
         "0}},\n");
  printf("            \"with_any_diff\": {\"type\": \"integer\", \"minimum\": 0}\n");
  printf("          }\n");
  printf("        },\n");
  printf("        \"cells\": {\n");
  printf("          \"type\": \"object\",\n");
  printf("          \"required\": [\"compared\",\"matched\",\"within_tolerance\",\"differing\"],\n");
  printf("          \"properties\": {\n");
  printf("            \"compared\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("            \"matched\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("            \"within_tolerance\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("            \"differing\": {\"type\": \"integer\", \"minimum\": 0}\n");
  printf("          }\n");
  printf("        },\n");
  printf("        \"by_column\": {\n");
  printf("          \"type\": \"array\",\n");
  printf("          \"items\": {\n");
  printf("            \"type\": \"object\",\n");
  printf("            \"required\": [\"name\",\"compared\",\"matched\",\"within_tolerance\",\"differing\"],\n");
  printf("            \"properties\": {\n");
  printf("              \"name\": {\"type\": \"string\"},\n");
  printf("              \"compared\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("              \"matched\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("              \"within_tolerance\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("              \"differing\": {\"type\": \"integer\", \"minimum\": 0}\n");
  printf("            }\n");
  printf("          }\n");
  printf("        },\n");
  printf("        \"schema\": {\n");
  printf("          \"type\": \"object\",\n");
  printf("          \"required\": [\"common\",\"only_in_input\"],\n");
  printf("          \"properties\": {\n");
  printf("            \"common\": {\"type\": \"array\", \"items\": {\"type\": \"string\"}},\n");
  printf("            \"only_in_input\": {\"type\": \"array\", \"items\": {\"type\": \"array\", \"items\": {\"type\": "
         "\"string\"}}}\n");
  printf("          }\n");
  printf("        }\n");
  printf("      }\n");
  printf("    },\n");
  printf("    \"rows\": {\n");
  printf("      \"type\": \"array\",\n");
  printf("      \"items\": {\n");
  printf("        \"oneOf\": [\n");
  printf("          {\n");
  printf("            \"type\": \"array\",\n");
  printf("            \"items\": {\n");
  printf("              \"oneOf\": [\n");
  printf("                {\"type\": [\"string\",\"null\"]},\n");
  printf("                {\"type\": \"array\", \"items\": {\"type\": [\"string\",\"null\"]}}\n");
  printf("              ]\n");
  printf("            }\n");
  printf("          },\n");
  printf("          {\n");
  printf("            \"type\": \"object\",\n");
  printf("            \"required\": [\"data\",\"missing_in\"],\n");
  printf("            \"properties\": {\n");
  printf("              \"data\": {\n");
  printf("                \"type\": \"array\",\n");
  printf("                \"items\": {\n");
  printf("                  \"oneOf\": [\n");
  printf("                    {\"type\": [\"string\",\"null\"]},\n");
  printf("                    {\"type\": \"array\", \"items\": {\"type\": [\"string\",\"null\"]}}\n");
  printf("                  ]\n");
  printf("                }\n");
  printf("              },\n");
  printf("              \"missing_in\": {\n");
  printf("                \"type\": \"array\",\n");
  printf("                \"items\": {\"type\": \"integer\", \"minimum\": 0},\n");
  printf("                \"description\": \"indices in ascending order\"\n");
  printf("              }\n");
  printf("            }\n");
  printf("          }\n");
  printf("        ]\n");
  printf("      }\n");
  printf("    }\n");
  printf("  }\n");
  printf("}\n");
}

static int zsv_compare_print_help_topic(const char *name) {
  /* canonical: json-redline, json-redline-schema
     silent aliases (undocumented): compare-json-redline, json-redline-json, compare-json-redline-schema
  */
  if (!strcmp(name, "json-redline") || !strcmp(name, "compare-json-redline")) {
    zsv_compare_print_help_topic_narrative();
    return 0;
  }
  if (!strcmp(name, "json-redline-schema") || !strcmp(name, "json-redline-json") ||
      !strcmp(name, "compare-json-redline-schema")) {
    zsv_compare_print_help_topic_json_schema();
    return 0;
  }
  fprintf(stderr, "Unknown help topic: %s\n", name);
  fprintf(stderr, "Available topics: json-redline, json-redline-schema\n");
  return 1;
}
