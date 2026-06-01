# `zsv compare --json-redline` — User Guide

## Overview

`zsv compare --json-redline` produces a self-contained JSON document that fully describes a comparison result.  A downstream tool can render an HTML or XLSX redline from this output alone, without re-reading the source CSVs.

## Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--json-redline` | off | Emit the enriched JSON schema instead of CSV or `--json` output. |
| `--include-unchanged-rows` | off | Also emit matched rows (all-scalar arrays) alongside diffed rows. |
| `--include-tolerated` | off | Render within-tolerance cells as diff arrays instead of collapsing them to input[0]'s scalar. |

These flags are independent of and do not affect `--json`, `--json-object`, or `--json-compact`.

## Quick Examples

### Two files, key-matched, default output (diffs only)

```sh
zsv compare --json-redline -k loan_id a.csv b.csv
```

Rows with at least one non-tolerated diff appear in `rows[]`.  All other rows are silently counted in `summary`.

### Include matched rows

```sh
zsv compare --json-redline -k loan_id --include-unchanged-rows a.csv b.csv
```

Every matched row appears as a bare scalar array, positionally aligned to `columns[]`.

### Suppress near-equal numeric differences

```sh
zsv compare --json-redline -k loan_id --tolerance 0.01 a.csv b.csv
```

Cells where both values are numeric and `|a − b| < 0.01` collapse to input[0]'s scalar.  The count is reported in `summary.cells.within_tolerance`.

### Expose tolerated cells as diff arrays

```sh
zsv compare --json-redline -k loan_id --tolerance 0.01 --include-tolerated a.csv b.csv
```

Tolerated cells appear as `["v0","v1"]` diff arrays (distinguishable from real diffs via `summary.cells.within_tolerance`).

### Three-input comparison

```sh
zsv compare --json-redline -k id a.csv b.csv c.csv
```

Diff arrays have three elements: `["a_val","b_val","c_val"]`.  All parallel arrays (`inputs[]`, `only_in_input_count[]`, etc.) are indexed by input position.

## Output Shape (v1 schema)

```text
{
  schema, version, generated_at,
  inputs[]:  { label, path, row_count },
  keys[],
  options:   { tolerance, sort, include_unchanged_rows, include_tolerated },
  columns[]: { name, is_key, in_inputs[] },
  summary:   {
    rows:      { in_all_inputs, only_in_input_count[], with_any_diff },
    cells:     { compared, matched, within_tolerance, differing },
    by_column[]: { name, compared, matched, within_tolerance, differing },
    schema:    { common[], only_in_input[][] }
  },
  rows[]:  array of row entries (see below)
}
```

### Row entry forms

**Array form** — row present in every input:
```json
["key_val", "cell1", ["diff_a", "diff_b"], "cell3"]
```

**Object form** — row missing in some inputs:
```json
{"data": ["key_val", "cell1", null], "missing_in": [1]}
```

### Cell forms

- **Scalar** (`string | null`) — matched, or tolerated-and-collapsed cell.
- **Array** `["v0","v1",...]` — diff cell, parallel to `inputs[]`.  `null` in a slot means that input lacks this cell.

## Row order

Without `--sort`: input[0]'s processing order (rows unique to later inputs appear at their natural key position).  
With `--sort`: key-sorted order.

## Schema differences (mismatched columns)

Columns present in only some inputs appear in `columns[]` with `in_inputs` listing which inputs have them.  Their cells are emitted as scalars (not diff arrays) and are excluded from `cells.compared`.  The full picture is in `summary.schema`.

## Passthrough options

All existing `zsv compare` options (`-k`, `--tolerance`, `--sort`, `--sort-in-memory`, `-e`) work as before.  `--json-redline` adds output metadata; it does not change comparison semantics.
