# `zsv compare --json-redline` — Technical Guide

## Wire format

The output is plain JSON (no comments).  The schema is documented with comments in `schema.jsonc` at the repository root.  The formal wire format version is `"version": "1"`.

## Schema cross-reference

| `schema.jsonc` section | Implementation |
|------------------------|----------------|
| `schema`, `version`, `generated_at` | Emitted by `zsv_compare_emit_enriched` using `time()`/`gmtime()`. |
| `inputs[]` | One entry per `struct zsv_compare_input`.  `label` = `basename(path)`, `path` = CLI argument as given, `row_count` = rows consumed by `zsv_compare_collect_row`. |
| `keys[]` | `data->keys` linked list. |
| `options` | Flags from `data->tolerance.original`, `data->sort`, `data->writer.include_unchanged_rows`, `data->writer.include_tolerated`. |
| `columns[]` | `output_colnames_first` linked list; `in_inputs` computed from `input->out2in[j] != 0`. |
| `summary.*` | Aggregated in `struct zsv_compare_enriched` during the comparison pass. |
| `rows[]` | `struct zsv_compare_enriched_row` linked list emitted in processing order. |

## Tolerance semantics

When `--tolerance T` is set, `data->tolerance.value` is stored as `nextafterf(T, INFINITY)` (slightly above the user value, matching existing compare behaviour).  `data->tolerance.original` retains the user-specified value and is emitted in `options.tolerance`.

A cell is *tolerated* when all pairs (input[0] vs input[i], i > 0) satisfy `fabs(d0 − di) < data->tolerance.value` after parsing both strings with `zsv_strtod_exact`.

Default mode collapses a tolerated cell to input[0]'s raw string scalar.  With `--include-tolerated`, the cell is emitted as a diff array and counts toward `rows_with_diff`.

## Schema-difference handling

A column present in only some inputs (`input->out2in[j] == 0` for at least one input) is treated as a schema-diff column:

- Cells are populated from the first present input — never as diff arrays.
- `col_stats[j].compared` stays 0 for that column.
- The column appears in `summary.schema.only_in_input[i]` for each input `i` that exclusively owns it.
- `summary.schema.common[]` lists columns present in all inputs.

## Memory Ledger

Every new allocation introduced by the enriched mode and its corresponding free path:

| Allocation site | Owning field | Free path |
|----------------|--------------|-----------|
| `calloc(1, sizeof(*e))` — the enriched context | `data->enriched` | `zsv_compare_enriched_free` → called from `zsv_compare_data_free` |
| `calloc(input_count, ...)` — `e->input_row_counts` | `enriched->input_row_counts` | `zsv_compare_enriched_free` |
| `calloc(input_count, ...)` — `e->rows_only_in_input` | `enriched->rows_only_in_input` | `zsv_compare_enriched_free` |
| `calloc(output_colcount, ...)` — `e->col_stats` | `enriched->col_stats` | `zsv_compare_enriched_free` |
| `calloc(1, sizeof(*row))` — per row | `enriched->rows_head` linked list | `zsv_compare_enriched_row_free` (called for every row by `zsv_compare_enriched_free`) |
| `calloc(output_colcount, ...)` — `row->cells` | `row->cells` | `zsv_compare_enriched_row_free` |
| `malloc(missing_count * ...)` — `row->missing_in` | `row->missing_in` | `zsv_compare_enriched_row_free` |
| `zsv_compare_enrich_strndup(v.str, v.len)` — `cell->scalar_s` | `cell->scalar_s` | `zsv_compare_enriched_cell_free` |
| `calloc(input_count, ...)` — `cell->diff_s` | `cell->diff_s` | `zsv_compare_enriched_cell_free` |
| `calloc(input_count, ...)` — `cell->diff_len` | `cell->diff_len` | `zsv_compare_enriched_cell_free` |
| `zsv_compare_enrich_strndup(...)` — per slot in `cell->diff_s[i]` | `cell->diff_s[i]` | `zsv_compare_enriched_cell_free` (loops over input_count) |

`zsv_compare_data_free` calls `zsv_compare_enriched_free` unconditionally (NULL-safe) before the existing JSON writer cleanup.

## Resolved open decisions

| Decision | Resolution |
|----------|-----------|
| Final flag name | `--json-redline` (consistent with `--json`, `--json-object`, `--json-compact` prefix) |
| Float formatting | Cell values are raw CSV strings — no float formatting. `options.tolerance` emits the user-specified value via `data->tolerance.original`. No floats are computed or emitted in `rows[]`. |
| Row order without `--sort` | Input[0]'s processing order; rows unique to later inputs appear at their natural key position in the output stream. |
| `only_in_input` shape for >2 inputs | Parallel array `[["cols_only_in_0"], ["cols_only_in_1"], ...]` indexed by input position — consistent with all other parallel arrays in the schema. |

## Code structure

New files:
- `app/compare_enriched.h` — struct definitions for `zsv_compare_enriched`, `zsv_compare_enriched_row`, `zsv_compare_enriched_cell`, `zsv_compare_col_stat`.

Modified files:
- `app/compare_internal.h` — added `data->enriched` pointer, `writer.include_unchanged_rows`, `writer.include_tolerated` bits, `tolerance.original` field.
- `app/compare.c` — added `#include <time.h>`, `ZSV_COMPARE_OUTPUT_TYPE_JSON_REDLINE 'e'`, and the following static functions:
  - `zsv_compare_enrich_strndup` — safe strdup for cell values.
  - `zsv_compare_enriched_new` / `_free` / `_cell_free` / `_row_free` — lifecycle.
  - `zsv_compare_collect_row` — called from `zsv_compare_print_row` in enriched mode; builds the in-memory row representation and updates stats.
  - `zsv_compare_emit_enriched` — emits the complete JSON document via `jsonwriter_*` at the end.
  - Hook in `zsv_compare_output_begin`: init `jsonwriter_new` only, no output yet.
  - Hook in `zsv_compare_output_end`: call `zsv_compare_emit_enriched`.
  - Hook in `zsv_compare_data_free`: call `zsv_compare_enriched_free`.

All hooks guard on `writer.type == ZSV_COMPARE_OUTPUT_TYPE_JSON_REDLINE` and are no-ops for all other modes.
