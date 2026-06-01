# `zsv compare --json-enriched` — Admin / Integration Guide

## Integration with the strat-replication redline composer

The redline composer runs one `zsv compare --json-enriched` per stratification table and receives one JSON document per comparison.  Integration steps:

1. **Run compare** for each table pair:
   ```sh
   zsv compare --json-enriched -k <key_col> actual.csv expected.csv > table_N.json
   ```

2. **Check `summary.rows.with_any_diff`** to decide whether a table has any differences.  If zero, the table is clean; skip rendering.

3. **Render a table block** from `rows[]`:
   - Use `columns[]` to label columns.
   - Rows where every cell is a scalar → no highlight.
   - Rows where a cell is an array → highlight differing cells (red for non-tolerated, yellow for tolerated).
   - Object-form rows (`missing_in` set) → mark the entire row as added/removed.
   - Use `summary.cells.within_tolerance > 0` to add a legend footnote.

4. **Compose the document** by aggregating table blocks, adding a table-of-contents, per-table tie-out percentages, and handing off to the `doc-to-html` / `doc-to-xlsx` renderer.

5. **Schema columns** (`in_inputs` ≠ all inputs): show as a schema-warning row in the table block.  Cells in these columns are scalars (not diffs) regardless of value.

## Integration with future `doc-to-html` / `doc-to-xlsx` renderers

The enriched JSON output is **not** the document-model format that `doc-to-html`/`doc-to-xlsx` will consume.  The composer translates `compare --json-enriched` output into a table block within a higher-level document model:

```
compare JSON → composer → document model (headings + table blocks) → doc-to-html / doc-to-xlsx
```

The compare JSON is a peer document, not the renderer's input format.  This keeps the renderer generic (it accepts any structured table, not compare-specific diffs).

## Consuming `generated_at`

The `generated_at` field is an ISO 8601 UTC timestamp (`"%Y-%m-%dT%H:%M:%SZ"`).  It changes on every run and should be excluded from byte-for-byte regression tests.  Strip it before comparison:

```sh
zsv compare --json-enriched ... | sed '/"generated_at"/d' > actual.json
cmp actual.json expected.json
```

This is exactly how the test harness (`app/test/Makefile`, rule `test-compare-enriched`) handles it.

## Schema versioning

The `"version": "1"` field in the output identifies the schema version.  Consumers should check this field and reject or warn on unknown versions.  Version bumps are reserved for breaking changes to the shape of `columns[]`, `rows[]`, or `summary`.  Additive fields (new keys in `options`, new fields in row objects) will not bump the version.

## Test harness integration

New test cases live in `app/test/Makefile` under the `test-compare-enriched` target, which is added to the `TESTS` list and runs as part of `make -C app test`.  Input fixtures are in `app/test/compare/jenrich*.csv`.  Expected outputs (with `generated_at` stripped) are in `app/test/expected/test-compare-enriched.out[1-7]`.

To add a new test case, follow the pattern:
```makefile
@(${PREFIX} $< --json-enriched ... ${REDIRECT1} ${TMP_DIR}/$@.outNraw && \
sed '/"generated_at"/d' ${TMP_DIR}/$@.outNraw > ${TMP_DIR}/$@.outN && \
${CMP} ${TMP_DIR}/$@.outN expected/$@.outN && ${TEST_PASS} || ${TEST_FAIL})
```
