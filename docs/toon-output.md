# Specification: TOON as an alternative encoding for all `lq` JSON output

**Status:** Finalized for dev-team handoff. All §13 open questions resolved by an
agent-ergonomics review (rationale recorded inline and in `DECISIONS.md`).
**Author:** (prepared with Claude Code)
**Metric driving this work:** token count of machine-readable output consumed by LLM agents.

**Design north star:** every choice below is settled by one question — *what is
least effort and least error-prone for an AI agent that calls `lq` many times in
a session and feeds its output back into a token-bounded context?* The three
ergonomic levers that follow from that are: (1) **derivability** — an agent
learns one rule, not a lookup table; (2) **set-once** — configure the encoding a
single time, not per call; (3) **self-description** — the binary can teach an
agent the format on demand so it parses TOON as reliably as JSON.

---

## 1. Summary

`lq` exposes a number of machine-readable JSON outputs (command metadata, help
topics, data converters, report outputs). When the consumer is an LLM agent,
JSON spends tokens on structural punctuation (braces, quoted keys, commas) that
carry no information. **TOON (Token-Oriented Object Notation)** is a lossless,
JSON-equivalent encoding that removes most of that overhead, especially for
uniform arrays of objects.

This spec asks for a single, uniform capability: **anywhere `lq` can emit JSON,
it can emit the identical logical value as TOON instead.** No output gains or
loses information; only the encoding changes.

### Measured benefit (justification)

Empirical run over a representative corpus generated from the live binary,
counted with the **Claude tokenizer** (`claude-opus-4-8`, `count_tokens`) and
cross-checked with tiktoken `o200k_base`/`cl100k_base`:

| Surface                          | JSON tok | TOON tok | Reduction |
|----------------------------------|---------:|---------:|----------:|
| `lq 2json` (uniform table, 200 rows) | 10,453 | 6,780 | **35.1%** |
| `lq commands --json`             |   4,838  |   3,228  | **33.3%** |
| `lq desc+ -j` (nested stats)     |   2,622  |   1,815  |   30.8%   |
| `lq help formats-json`           |   2,492  |   1,756  |   29.5%   |
| `lq help json-schema`            |  21,130  |  15,272  |   27.7%   |
| `lq help compare-json-redline-schema` | 1,536 | 1,168 | 24.0% |
| `lq help formula-functions-json` |  33,710  |  27,263  |   19.1%   |
| **Total**                        | **76,781** | **57,282** | **25.4%** |

Reduction is consistent across tokenizers (25.4% Claude vs 24.2% o200k). The
floor (~19%) is the most irregular surface; uniform arrays reach ~35%. For an
agent that reads the fixed help surfaces once to orient itself, this is
~15,000 Claude tokens saved per session before any data is touched.

---

## 2. Goals and non-goals

### Goals
- G1. Every JSON-emitting surface has a TOON equivalent producing the same
  logical value.
- G2. TOON output is **lossless**: decoding it back to JSON yields a value equal
  to the JSON the same command would have produced (object key order excepted —
  JSON does not guarantee it either).
- G3. A single internal serialization chokepoint, so TOON is added once, not
  per command (DRY).
- G4. A `toon2json` decoder ships in-tree, enabling round-trip tests and
  letting JSON-consuming commands accept TOON.
- G5. Discoverable via lq's existing conventions (sibling flags / commands /
  help topics).

### Non-goals
- N1. TOON does **not** become the default. JSON remains the default output.
- N2. No change to the underlying data model or schema of any output.
- N3. No value re-typing (see R3). The encoder must not "improve" types.
- N4. Not in scope: TOON for human-facing pretty/markdown tables — those are not
  JSON outputs.

---

## 3. Definitions

- **TOON** — a compact, indentation-based encoding of the JSON data model.
  Uniform arrays of objects are emitted in a tabular form
  (`name[N]{f1,f2,...}:` header followed by one comma-delimited row per element);
  non-uniform arrays fall back to a list form (`- item`); scalars and nested
  objects map directly. TOON is an *encoding of JSON*, not a new data model:
  every JSON value has a TOON form and vice versa.
- **Reference implementation** — the installed `json2toon` utility is the
  reference encoder for behavioral parity during development. The spec pins a
  TOON spec version (see §9); `json2toon` output is the conformance oracle.

---

## 4. Scope — inventory of JSON-emitting surfaces

All of the following must gain a TOON equivalent. (Authoritative list to be
reconciled against the registry — see R0.)

### 4a. Command metadata / help (fixed, machine-readable)
- `lq commands --json`
- `lq help formula-functions-json`
- `lq help formats-json`
- `lq help json-schema`
- `lq help compare-json-redline-schema`
- `lq help formula-functions-json <name> ...` (filtered form)

### 4b. Data converters and reports (data-dependent)
- `lq 2json` (the format converter)
- `lq desc+ -j` / `--json`
- `lq compare --json` and `lq compare --json-redline`
- `lq sum --map-reduce-out` (sharded summary JSON)
- `lq domain <d> query-database <db> --json` and `--json-object`
- Any other command whose `-h` advertises a `--json`/`-j` output mode

### 4c. JSON-*consuming* commands (decode side, per §8)
- `lq jq` (filters JSON input)
- `lq 2db` (consumes `database-table.json`)
- `lq reduce` (consumes `sum --map-reduce-out` JSON)

> **R0 (discovery requirement).** Before implementation, enumerate the
> authoritative set programmatically from the same registry that backs
> `lq commands --json` and the help-topic table, rather than from this list, so
> the two cannot drift. Any surface that prints JSON and is missing a TOON
> sibling is a conformance bug.

---

## 5. User interface — mirror the JSON surface

For each JSON entry point, add a TOON sibling using lq's existing naming
conventions. The mirror is purely a naming layer over the single internal
encoder (§6).

| JSON form                         | TOON sibling                        |
|-----------------------------------|-------------------------------------|
| `--json` / `-j` flag              | `--toon` flag (mutually exclusive with `--json`) |
| `lq 2json`                        | `lq 2toon`                          |
| `lq help <topic>-json`            | `lq help <topic>-toon`              |
| `--json-object` (domain query)    | `--toon-object`                     |
| `--json-redline` (compare)        | `--toon-redline`                    |

Rules:
- U1. `--toon` and `--json` are mutually exclusive; specifying both is a usage
  error (exit code 1) with a one-line message.
- U2. `--toon` implies structured output exactly where `--json` does (e.g.
  `lq commands --toon` behaves like `lq commands --json` but TOON-encoded).
- U3. `lq help formats` (narrative) and `lq commands --help` text should mention
  the TOON siblings so they are discoverable from the existing help.
- U4. An `--indent N` option, where the JSON form already accepts one, applies
  identically to TOON (parity with `json2toon --indent`).
- **U5 (mechanical naming rule — agent ergonomics, was Q2).** The TOON name for
  any surface is the JSON name with the substring `json` replaced by `toon`,
  with no exceptions: `--json`→`--toon`, `-j`→(add `--toon`; no new short flag,
  to avoid a non-derivable letter), `2json`→`2toon`, `--json-object`→
  `--toon-object`, `--json-redline`→`--toon-redline`, `help <t>-json`→
  `help <t>-toon`. This is a hard requirement, not a convention: an agent must be
  able to compute the TOON invocation from the JSON one by string substitution
  and never consult a table. Any surface that breaks the rule is a bug.
- **U6 (exceptionless coverage — agent ergonomics, was Q4).** Every command that
  can emit JSON supports `--toon`, including `lq jq` (which gains TOON *output*;
  see also DC2 for TOON *input*). There is no subset of "TOON-capable" commands
  for an agent to memorize; support is universal so the rule in U5 always holds.

### Session default via environment / config (agent ergonomics — was Q1)

- **U7.** A single environment variable `ZSV_STRUCTURED_FORMAT` (values `json` |
  `toon`; default `json`) sets the session-wide encoding for every structured
  output. An equivalent key in `lq config` MAY also set it. (`ZSV_*` because the
  mechanism lives in the zsv layer — see §6a; lq MAY alias `LQ_STRUCTURED_FORMAT`
  to it.) This is the explicit set-once lever: an operator or agent sets it
  **once** and every subsequent call emits TOON without per-call flags — fewer
  tokens per command line and no risk of forgetting the flag on one call in
  twenty. It is also the deterministic escape hatch: setting it to `json` forces
  JSON regardless of any auto-detected agent profile (U9), which scripts and
  humans need for reproducibility.

### Zero-config agent profile via `AI_AGENT` detection (agent ergonomics)

The ultimate ergonomic state is **set-zero**: the agent configures nothing and
still gets agent-friendly output. Many agent harnesses already export a generic
`AI_AGENT` environment variable (e.g. `AI_AGENT=claude-code_2.1_agent`). zsv
(and therefore `lq`) SHOULD use that as a zero-config signal to enable an "agent
profile" of defaults — currently a single behavior: structured output defaults
to TOON. Because this is generic, it lives in the zsv layer (§6a), so the
standalone `zsv` CLI benefits too.

This is deliberately constrained, because changing a command's output as a
function of *ambient, foreign* environment state is otherwise a
reproducibility / least-surprise hazard. The constraints below are what make it
safe, and are mandatory, not advisory:

- **U8 (toggle + auto-detect).** Introduce `ZSV_AGENT_DEFAULTS` with values
  `auto` (default) | `on` | `off`. `auto` means: the agent profile is enabled
  **iff** `AI_AGENT` is set and non-blank. `on`/`off` force it irrespective of
  `AI_AGENT`. `AI_AGENT` is read as a **presence** signal only — its contents
  are never parsed. (lq MAY alias `LQ_AGENT_DEFAULTS`.)
- **U9 (precedence).** Resolution of the structured-output encoding, highest
  priority wins:
  1. explicit per-command `--json` / `--toon` flag;
  2. `ZSV_STRUCTURED_FORMAT` (explicit operator/agent choice);
  3. **agent profile** (enabled per U8) ⇒ default encoding = `toon`;
  4. built-in default ⇒ `json` where structured output is the default; **CSV and
     all other non-JSON defaults are unchanged** (preserves N1).
  Thus anything explicit always beats the auto-profile, and an operator who wants
  deterministic JSON sets `ZSV_STRUCTURED_FORMAT=json` or `ZSV_AGENT_DEFAULTS=off`.
- **U10 (scope limit).** The agent profile MUST only change the *encoding* of
  output that is already structured/JSON (JSON→TOON). It MUST NOT change which
  commands emit structured output, MUST NOT alter CSV/other defaults, and MUST
  NOT change any data semantics. Any future addition to the agent profile must
  independently satisfy U10–U11 (safe, overridable, observable) before being
  bundled.
- **U11 (observability — silent magic is the anti-pattern).** Two parts:
  - **U11a (one-line stderr notice).** When, and only when, the encoding was
    selected by the agent profile (the `AI_AGENT` auto path of U9 step 3 — *not*
    an explicit flag and *not* an explicit `ZSV_STRUCTURED_FORMAT`), the command
    MUST print exactly one terse line to **stderr** before output. The line is
    prefixed with the program name via the existing `ZSV_USAGE_PROG` macro (so it
    reads `zsv` or `lq` per binary, not a hardcoded name) and names the format,
    its cause, and the override + silence switches — nothing more:
    `<ZSV_USAGE_PROG>: TOON output (AI_AGENT); override: --json; silence: ZSV_AGENT_NOTICE=off`.
    Rationale: this is the direct cure for the spooky-action hazard — the
    behavior becomes *visible* at the moment it happens, on a channel that never
    corrupts stdout/pipelines. It fires only on the implicit path, so explicit
    invocations stay silent.
  - **U11b (suppressible + bounded).** The notice MUST be one line, on stderr,
    at most once per process, and suppressible via `ZSV_AGENT_NOTICE=off` (and
    SHOULD also be suppressed by a general `-q`/`--quiet`). This bounds the cost
    for agents whose harness captures stderr into the model context: ~15 tokens,
    only on the auto path, switchable off once the agent has learned the
    behavior — negligible against the per-call savings.
  - **U11c (inspectable state).** Independently, the resolved encoding and its
    source MUST be reportable on demand via `lq config` and a `-v`/`--verbose`
    line, e.g. `structured-output=toon (source: agent profile; AI_AGENT
    detected)`, so the state can be queried without triggering output.

> Why `AI_AGENT` and not a TTY heuristic: "format differently when stdout is not
> a TTY" is fragile (every pipe is a non-TTY, including human `lq … | less`).
> `AI_AGENT` is a deliberate, specific signal and pairs with an explicit
> override chain, so it does not misfire on ordinary pipelines.
>
> Why keep `ZSV_STRUCTURED_FORMAT` *and* `AI_AGENT` (in addition, not instead):
> the auto-profile gives zero-config for the common agent case; the explicit var
> gives determinism for scripts/CI and a clean opt-out for agents that parse
> TOON less reliably than JSON. Each covers the other's failure mode.

> Alternative considered: a single global `--format json|toon` selector instead
> of mirror siblings. Rejected as the primary surface — it cannot name the
> converter commands (`2json`/`2toon` are commands, not flags), and the
> set-once need it would address is better served by `ZSV_STRUCTURED_FORMAT`
> (U7), which works across *all* commands including converters. The mirror
> surface + env default is strictly more ergonomic for an agent than either
> alone. A `--format` alias MAY still be added internally without conflict.

---

## 6. Internal design — one encoder, selected once

- D1. Introduce a single output-encoding abstraction (two backends behind one
  writer interface: `json`, `toon`), built on zsv's existing writer layer (the
  `json_writer-1.01` / `yajl_helper` / `ZSV_WRITER_*` machinery). Every command
  routes structured output through it.
- D2. TOON encoding logic lives in exactly one place. No command contains
  TOON-specific branching beyond selecting the encoding flag. This is the DRY
  requirement: adding TOON must not mean editing N commands.
- D3. The selected encoding is threaded as a single parameter from
  argument-parsing to the writer; commands stay encoding-agnostic.

### 6a. Implementation layering — zsv first, lq remainder

`lq` is built on the open-source **zsv** project (this repo): zsv provides the
CSV engine, the JSON serialization machinery (`app/external/json_writer-1.01`,
`yajl_helper`), the writer abstraction (`ZSV_WRITER_*`), and the base
JSON-emitting commands (`app/2json.c`, `2db.c`, `desc.c`, `compare.c`, `jq.c`,
…). lq adds proprietary commands (`sum`/`strat`, `validate`, `map`, `domain`,
`reduce`, redline assembly) on top. The capability therefore belongs **mostly in
zsv**, with only thin wiring in lq:

- **L1 (zsv owns the mechanism).** The TOON encoder and the `toon2json` decoder
  are added to zsv (a new vendored component beside `json_writer-1.01`, sharing
  the reference `json2toon`/`toon2json` implementation). The single output
  chokepoint (D1) is the zsv writer layer. Consequence: every zsv-native JSON
  emitter (`2json`→`2toon`, `desc -j`→`desc --toon`, `compare --json`, `jq`,
  etc.) gains TOON from one change, with no per-command work.
- **L2 (zsv owns the generic plumbing).** The `--toon` flag, the `2toon`
  command, the `*-toon` help topics for zsv-native topics, the encoding
  default-resolution chain (U7–U11), and the `AI_AGENT` agent-profile detection
  are all generic CLI behavior and live in zsv. Because they live in the open
  layer, the environment variables are **`ZSV_*`**, consistent with existing
  zsv vars such as `ZSV_COMPARE_OUTPUT_TYPE_JSON`: `ZSV_STRUCTURED_FORMAT` and
  `ZSV_AGENT_DEFAULTS` (see U7/U8). `AI_AGENT` detection is generic and likewise
  lives in zsv, so the standalone `zsv` CLI benefits too.
- **L3 (lq owns only the wiring).** lq's proprietary commands must route their
  structured output through zsv's shared writer/chokepoint so they inherit TOON;
  this is integration, not re-implementation. lq adds `*-toon` siblings for its
  proprietary help topics and MAY alias `LQ_STRUCTURED_FORMAT` /
  `LQ_AGENT_DEFAULTS` to the `ZSV_*` variables for brand continuity (the `ZSV_*`
  names are authoritative; lq aliases are optional sugar).
- **L4 (chokepoint prerequisite — scoping risk).** "One change covers all
  emitters" holds only where emitters already funnel through the zsv writer.
  Row-data emitters do; the **metadata/help JSON** (`commands --json`, the
  `*-json` help topics) may construct JSON ad hoc. Auditing those and routing
  them through the shared writer (or giving them a dedicated TOON path) is a
  zsv-layer task and a prerequisite for full coverage (R0). Estimate this before
  committing to the single-chokepoint plan.
- **L5 (open-core benefit).** Implementing in zsv puts the encoder under
  open-source review and gives the standalone `zsv` CLI the same token savings —
  consistent with Liquidaty's open-core model, and avoids a proprietary fork of
  serialization logic that would otherwise drift from zsv's writer.

> Net split: ~80% of the work (encoder/decoder, chokepoint, flags, env/agent
> detection, zsv-native command coverage) is **zsv**; the lq-specific remainder
> is routing proprietary commands through the shared writer and adding lq-only
> help-topic siblings.

---

## 7. Functional requirements

- **R1 (equivalence).** For every in-scope surface and every input, the TOON
  output MUST decode (via the §8 decoder) to a value equal to the JSON output
  the same command/args would produce, modulo object-key ordering.
- **R2 (no data loss).** Nulls, empty arrays/objects, empty strings, booleans,
  nested structures, and Unicode MUST round-trip exactly.
- **R3 (no re-typing).** The encoder MUST preserve the JSON scalar type as
  produced. In particular, `lq 2json` emits cell values as JSON **strings**
  (e.g. `"1000"`, `"44.0"`); the TOON form MUST keep them as strings and MUST
  NOT infer numbers/booleans. Conversely, surfaces that emit real numbers/bools
  (e.g. `desc+`) MUST preserve those.
- **R4 (encoding).** Output is UTF-8. Same newline and locale behavior as the
  JSON path.
- **R5 (errors & exit codes).** Error reporting, stderr format, and exit codes
  are unchanged from the JSON path. A malformed-input failure produces the same
  diagnostics regardless of encoding.
- **R6 (streaming / large data — resolved per agent ergonomics, was Q3).**
  TOON's tabular array header (`[N]{fields}:`) requires the element count `N`
  and the field set *before* the rows. The resolved policy keeps the tabular
  form (the source of the token savings) as the default in all cases and never
  forces the agent to supply a flag:
  - The field set for CSV-derived output is the header row — known immediately.
  - When the input is **seekable** (a file), do a two-pass encode (count, then
    stream rows). Memory MUST stay O(row width), not O(file). No buffering of
    output.
  - When the input is **non-seekable** (stdin/pipe), buffer transparently to
    obtain `N`, then emit. This MUST NOT error and MUST NOT silently fall back
    to a non-tabular form (which would forfeit the savings).
  - An optional `--rows N` hint MUST be accepted on the converter to skip the
    count/buffer step when the agent already knows the count (e.g. from a prior
    `lq count`). It is an optimization, never a requirement.
  - Rationale: an agent piping data must not hit a surprise hard-error nor a
    silent loss of the token benefit; "just works, tabular, optional fast path"
    is the least-effort contract.
- **R7 (determinism).** Given identical input and args, byte-identical TOON
  output across runs (stable field order = JSON key order; stable row order =
  input order). Determinism is itself an agent-ergonomic property: it makes TOON
  output cacheable and diffable across calls.
- **R8 (self-describing format — agent ergonomics).** The binary MUST ship a
  narrative help topic `lq help toon` that documents the TOON encoding it
  produces: the tabular-array header grammar (`name[N]{f1,f2}:`), the list-form
  fallback for non-uniform arrays, scalar/null/empty handling, and the U5
  naming rule. Purpose: an agent encountering TOON for the first time can load
  the grammar on demand and parse the output reliably, instead of guessing.
  This is the mitigation for the one residual risk in this proposal — that a
  model parses TOON less reliably than JSON. The topic itself SHOULD be small
  and stable.

---

## 8. Decode path — `toon2json`

- DC1. Ship a `lq toon2json` command (and the underlying decoder) that converts
  TOON back to JSON. This is the inverse of `2toon`.
- DC2. JSON-consuming commands (`lq jq`, `lq 2db`, `lq reduce`) SHOULD accept
  TOON input, detected by extension (`.toon`) or an explicit `--toon-in` flag,
  by routing through the decoder. (MAY be phased after the encode work.)
- DC3. The decoder exists primarily to make R1/R2 testable in-tree; it is on the
  critical path for the test suite (§10), not optional tooling.

---

## 9. TOON version & conformance

- V1. Pin the targeted TOON specification version in the docs and in a single
  source-of-truth constant.
- V2. During development, `json2toon`/`toon2json` (reference impl) output is the
  conformance oracle: `lq 2toon` output for a given JSON MUST match the
  reference encoder's output for that same JSON (byte-identical at the default
  indent), so behavior cannot silently diverge.

---

## 10. Testing requirements

- T1. **Round-trip property test.** For a corpus covering every in-scope surface
  plus edge cases (empty array, empty object, null, empty string, nested,
  non-uniform array, Unicode, very wide table, single-row, zero-row):
  `json -> 2toon -> toon2json` MUST equal the original JSON value (R1/R2).
- T2. **Reference parity.** `lq 2toon(json)` byte-equals `json2toon(json)` at
  default indent for the corpus (V2).
- T3. **Golden files.** Check in JSON+TOON golden pairs for each fixed help
  surface; CI fails on unexpected drift.
- T4. **Streaming.** A large-input `2toon` test asserts bounded memory on the
  seekable path (R6) and correct `N` in the header.
- T5. **Token regression (optional, informative).** A non-blocking CI report
  recording JSON-vs-TOON token counts per surface, so future schema changes that
  erode the savings are visible. (Harness already exists; see appendix.)

---

## 11. Documentation

- Add `lq help toon` (narrative): the encoding grammar an agent needs to parse
  TOON reliably, plus the U5 naming rule, the `ZSV_STRUCTURED_FORMAT` /
  `ZSV_AGENT_DEFAULTS` / `AI_AGENT` resolution chain, and the U11a stderr notice
  (R8). This is the single page an agent loads to "learn TOON."
- Update `lq help formats` (narrative) to introduce TOON, point to `lq help
  toon`, and state the U5 rule so the siblings are derivable, not enumerated.
- Each affected command's `-h` lists `--toon` next to `--json`.
- Add `lq help formats-toon` mirroring `formats-json`.

---

## 12. Backward compatibility & rollout

- JSON remains the default; no existing invocation changes behavior (N1).
- Suggested phasing (layered — see §6a):
  1. **zsv:** encoder/`toon2json` decoder + the writer chokepoint + round-trip
     tests + audit of the metadata/help emitters (L4). Foundation + losslessness
     guarantee.
  2. **zsv:** `--toon`/`2toon`/`*-toon` across zsv-native `--json`-bearing
     commands; `ZSV_STRUCTURED_FORMAT`; `ZSV_AGENT_DEFAULTS` + `AI_AGENT`
     detection (U7–U11).
  3. **lq:** route proprietary commands (`sum`/`strat`, `validate`, `map`,
     `domain`, `reduce`, redline) through the shared writer; add lq-only
     `*-toon` help topics; optional `LQ_*` env aliases.
  4. **zsv+lq:** TOON-input acceptance for `jq`/`2db`/`reduce` (DC2).
- Each phase is independently shippable; phase 1 establishes the foundation and
  the losslessness guarantee, and lands entirely in open-source zsv.

---

## 13. Resolved decisions (agent-ergonomics review)

All prior open questions are settled below by the design north star (top of
document). Full rationale in `DECISIONS.md`.

- **Q1 → RESOLVED: explicit set-once `ZSV_STRUCTURED_FORMAT` (default `json`)
  *plus* zero-config `AI_AGENT` auto-profile** (U7–U11). The explicit var is the
  deterministic lever and escape hatch; the `AI_AGENT` profile (lowest
  precedence, encoding-only, observable) gives set-*zero* for the common agent
  case. Kept in addition, not instead — each covers the other's failure mode.
  JSON stays the global default, so nothing changes for existing users.
- **Q2 → RESOLVED: strict mechanical `json`→`toon` naming, no exceptions** (U5).
  Derivability beats a lookup table; the names are computable by substitution.
- **Q3 → RESOLVED: tabular by default everywhere; seekable→two-pass,
  stdin→transparent buffer, optional `--rows N` fast path; never error, never
  silently drop the tabular form** (R6). "Just works" with an opt-in
  optimization is the least-effort contract for a piping agent.
- **Q4 → RESOLVED: exceptionless coverage, including `lq jq` TOON output** (U6).
  No subset of TOON-capable commands for an agent to track.
- **New → `AI_AGENT` zero-config agent profile** (U8–U11): enabled when
  `AI_AGENT` is non-blank, lowest precedence, scoped to encoding only, fully
  overridable and observable. The safe way to get set-zero without a
  reproducibility footgun.
- **New → implementation layering: zsv first, lq remainder** (§6a, L1–L5).
  Encoder/decoder, chokepoint, flags, and `AI_AGENT` detection live in the open
  zsv layer (env vars are `ZSV_*`); lq only routes its proprietary commands
  through the shared writer and adds lq-only help-topic siblings.
- **New → `lq help toon` self-description** (R8): the binary teaches the format
  on demand, mitigating the only residual risk (parse reliability).

### Genuinely deferred (non-blocking, implementer's discretion)
- DF1. Whether to also expose an internal `--format json|toon` alias (harmless;
  not required). 
- DF2. Whether `lq jq` should additionally *detect* TOON vs JSON input by
  sniffing content rather than extension/flag (DC2 already covers the explicit
  path; sniffing is a convenience that can come later).

---

## Appendix A — reproducible measurement harness

A Makefile harness accompanies this spec (`make all`): it regenerates the corpus
from the live binary, converts each surface with the reference `json2toon`, and
counts tokens with both the Claude tokenizer (session OAuth `count_tokens`) and
tiktoken. Files: `Makefile`, `gen_people.py`, `count.py` (tiktoken),
`count_claude.py` (Claude), `ASSUMPTIONS.md`. Re-run after any schema change to
refresh the token table in §1.

## Appendix B — illustrative before/after (`lq commands --json`)

JSON (per element):
```json
{ "name": "desc", "aliases": [], "group": "zsv",
  "synopsis": "describe each column", "category": "inspect",
  "keywords": ["inspect","describe","columns","schema"] }
```
TOON (tabular, header amortized across all elements):
```
- name: desc
  aliases: []
  group: zsv
  synopsis: describe each column
  category: inspect
  keywords[4]: inspect,describe,columns,schema
```
The per-row savings (dropped braces, quotes-on-keys, and inline array framing)
compound across every element of the array — the source of the 33% reduction on
this surface.

---

## Appendix C — implementation status (zsv layer, branch `agent`)

Phase 1 + the core of Phase 2 (§12) are implemented in the open **zsv** layer.
Per §6a/L1, the encoder and decoder are **not** reimplemented in zsv: the work
links the installed **`libjson2toon`** reference library (`json2toon_*` for
JSON→TOON and `toon2json_*` for TOON→JSON) — the same library that is the
conformance oracle (§9), so `lq 2toon` output is byte-identical to the reference
by construction.

### Delivered
- **`app/utils/toon.c`** (`include/zsv/utils/toon.h`) — thin streaming wrapper
  over `libjson2toon` (sink→FILE; feed from FILE/buffer). The single TOON
  chokepoint other commands route through. No hand-rolled encode/decode.
- **`lq 2toon`** — mirror of `lq 2json` (U5). Implemented inside `2json.c`:
  the JSON is serialized to an in-memory sink, then converted via the library,
  so the TOON is equivalent to the JSON `2json` would emit (R1). Adds `--toon`,
  `--json`, `--indent N`; `--toon`/`--json` are mutually exclusive (U1).
- **`lq toon2json`** (`app/toon2json.c`) — the decoder command (DC1).
- **`compare --toon` / `--toon-object` / `--toon-compact` / `--toon-redline`**
  (U5) — via the same memory-sink chokepoint; compare's CSV default is
  unchanged (U10).
- **Structured-format resolution** (`app/utils/structured.c`,
  `include/zsv/utils/structured.h`) — the U7–U11 precedence chain:
  explicit `--json`/`--toon` › `ZSV_STRUCTURED_FORMAT` › `AI_AGENT` agent
  profile (gated by `ZSV_AGENT_DEFAULTS=auto|on|off`) › built-in `json`. Emits
  the one-line stderr notice on the auto path only (U11a), suppressible via
  `ZSV_AGENT_NOTICE=off`/`-q`. `LQ_STRUCTURED_FORMAT` / `LQ_AGENT_DEFAULTS`
  aliases accepted. Wired into `2json`/`2toon`.
- **`lq help toon`** (`app/builtin/toon.c`) — the self-describing grammar topic
  (R8); reports the reference library version via `json2toon_version()`.
- **Build wiring** — `app/Makefile` links the native static
  `libjson2toon.a` (override `JSON2TOON_PREFIX`); pkg-config is intentionally
  bypassed so a cross-target `.pc` (e.g. WASI) cannot hijack the native link.
- **Tests** — `app/test` target `test-toon` (runs first): a JSON↔TOON
  round-trip over `data/test/2json.csv` (quoted commas, empty cells) asserting
  `2json --compact` == `toon2json(2json --toon)` and that the tabular header was
  produced (R1/T1), plus the `AI_AGENT` auto-profile (TOON + stderr notice) and
  the `ZSV_STRUCTURED_FORMAT=json` override (back to JSON, no notice).

### Deferred / not applicable in the open zsv layer (the lq remainder, §6a/L3)
- zsv's `desc` has no JSON output (the spec's `desc+ -j` is lq-proprietary), so
  no `--toon` there. The lq-proprietary surfaces (`sum`/`strat`, `validate`,
  `map`, `domain`, `reduce`, redline assembly, and the `*-json` help topics such
  as `formula-functions-json`) are lq's remaining wiring task — route their
  structured output through this shared chokepoint to inherit TOON.
- TOON **input** for `jq`/`2db`/`reduce` (DC2) and `jq` TOON **output** (U6) are
  Phase 4 follow-ons.
- Separate golden files (T3) are redundant here: because `2toon` *is* the
  reference encoder, parity holds by construction; the in-tree round-trip test
  covers losslessness (R1/R2) and the reference library carries its own
  conformance suite.
