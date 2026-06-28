# Assumptions & method notes

## Metric
Token count of `lq` output, JSON vs TOON. Lower = more ergonomic for an LLM consumer.
Reported as absolute Δtokens and % reduction, per surface and aggregate.

## Tooling
- Conversion: `json2toon` (installed), canonical encoder, default 2-space indent.
- Tokenizer: tiktoken `o200k_base` (GPT-4o/5-class) primary, `cl100k_base` cross-check.
  - The live Claude `count_tokens` API was intended as primary but the available
    ANTHROPIC_API_KEY is harness-scoped and returns 401 on direct calls. The %
    reduction is tokenizer-robust (it comes from removing structural redundancy),
    so o200k/cl100k are a sound proxy; both agree to within 0.3 pts.

## Corpus
Two classes, kept separate because TOON behaves differently on each:
- Fixed metadata/help (reproducible, read to LEARN the CLI):
  commands, formula-functions, formats, json-schema, redline-schema.
- Data-dependent (shape-driven): people-2json (uniform table), people-descplus (nested stats).
  Synthetic 200-row people.csv, seeded (gen_people.py) for reproducibility.

## Fidelity / losslessness
- `json2toon` output spot-checked structurally (element counts, field names, first/last
  rows present). NO `toon2json` decoder is installed, so a programmatic round-trip
  (toon->json deep-equal) was NOT performed. A production eval MUST add this.

## Known limitations (do not overclaim)
- `lq 2json` default emits the compact database-table shape (header array + row arrays),
  which already avoids repeated keys; its object-shape variant would compress far more
  under TOON. Result is mode-specific.
- Token count is a PROXY for ergonomics. It ignores whether the consuming model parses
  TOON as reliably as JSON. Pair with an extraction-accuracy check before acting on it.
- TOON's win is concentrated in uniform arrays; nested schemas benefit less.
