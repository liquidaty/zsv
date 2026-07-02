# `zsv compare --redline` — Admin / Integration Guide

## Integration with the strat-replication redline composer

The redline composer runs one `zsv compare --redline` per stratification table and receives one JSON document per comparison.  Integration steps:

1. **Run compare** for each table pair:
   ```sh
   zsv compare --redline -k <key_col> actual.csv expected.csv > table_N.json
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

The redline JSON output is **not** the document-model format that `doc-to-html`/`doc-to-xlsx` will consume.  The composer translates `compare --redline` output into a table block within a higher-level document model:

```
compare JSON → composer → document model (headings + table blocks) → doc-to-html / doc-to-xlsx
```

The compare JSON is a peer document, not the renderer's input format.  This keeps the renderer generic (it accepts any structured table, not compare-specific diffs).

## Consuming `generated_at`

The `generated_at` field is an ISO 8601 UTC timestamp (`"%Y-%m-%dT%H:%M:%SZ"`).  By default it changes on every run.  For reproducible output, set the `SOURCE_DATE_EPOCH` environment variable (UNIX epoch seconds, the reproducible-builds convention) to pin it to a fixed value:

```sh
SOURCE_DATE_EPOCH=1700000000 zsv compare --redline ...   # generated_at = 2023-11-14T22:13:20Z
```

When `SOURCE_DATE_EPOCH` is unset or invalid, the current time is used.  Alternatively, strip the field before byte-for-byte comparison:

```sh
zsv compare --redline ... | sed '/"generated_at"/d' > actual.json
cmp actual.json expected.json
```

This is exactly how the test harness (`app/test/Makefile`, rule `test-compare-redline`) handles it.

## Schema versioning

The `"version": "1"` field in the output identifies the schema version.  Consumers should check this field and reject or warn on unknown versions.  Version bumps are reserved for breaking changes to the shape of `columns[]`, `rows[]`, or `summary`.  Additive fields (new keys in `options`, new fields in row objects) will not bump the version.

## Test harness integration

New test cases live in `app/test/Makefile` under the `test-compare-redline` target, which is added to the `TESTS` list and runs as part of `make -C app test`.  Input fixtures are in `app/test/compare/jredline*.csv`.  Expected outputs (with `generated_at` stripped) are in `app/test/expected/test-compare-redline.out[1-7]`.

To add a new test case, follow the pattern:
```makefile
@(${PREFIX} $< --redline ... ${REDIRECT1} ${TMP_DIR}/$@.outNraw && \
sed '/"generated_at"/d' ${TMP_DIR}/$@.outNraw > ${TMP_DIR}/$@.outN && \
${CMP} ${TMP_DIR}/$@.outN expected/$@.outN && ${TEST_PASS} || ${TEST_FAIL})
```
