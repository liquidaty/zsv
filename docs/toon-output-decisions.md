# TOON-output spec — agent-ergonomics decisions & assumptions

Companion to `toon-output.md` (the spec). Records *why* each previously-open
question was resolved the way it was, and the assumptions made while working
autonomously. The deciding lens throughout: **least effort and least error for
an AI agent that calls `lq` many times per session and feeds output back into a
token-bounded context.**

## The three ergonomic levers (the basis for every decision)

1. **Derivability** — an agent should learn one rule, not a lookup table.
2. **Set-once** — configure the encoding a single time, not on every call.
3. **Self-description** — the binary can teach the format on demand, so the
   agent parses TOON as reliably as JSON.

## Decisions

| # | Question | Decision | Why (agent ergonomics) |
|---|----------|----------|------------------------|
| Q1 | Default via env/config, or always explicit? | Explicit `ZSV_STRUCTURED_FORMAT=json\|toon` (default `json`) **plus** zero-config `AI_AGENT` auto-profile; explicit flag overrides both. | Set-once *and* set-zero. The explicit var is the deterministic escape hatch; the `AI_AGENT` profile means the common agent case needs no config at all. Kept in addition, not instead — each covers the other's failure mode. Default stays JSON. |
| Q2 | Exact sibling flag/topic names | Strict mechanical `json`→`toon` substring rule, no exceptions (U5). | Derivability. The agent computes the TOON name from the JSON name; no table, no special cases to memorize or get wrong. |
| Q3 | Non-seekable stdin large-input policy | Tabular by default always; seekable→two-pass, stdin→transparent buffer; optional `--rows N` fast path; never error, never silently drop tabular form (R6). | A piping agent must not hit a surprise hard-error (requiring a flag it didn't know) nor a silent loss of the token savings that motivate the whole feature. "Just works + optional optimization" is the least-effort contract. |
| Q4 | Should `lq jq` also emit TOON? | Yes; coverage is exceptionless including `jq` (U6). | No subset of "TOON-capable" commands for the agent to track. Universality is what makes the U5 rule safe to rely on. |
| New | Zero-config without a spooky-action footgun? | `AI_AGENT` enables the profile at **lowest precedence**, **encoding-only**, fully overridable (`ZSV_AGENT_DEFAULTS=off`, explicit flags), **observable** (U8–U11). | Auto-detection is only safe if it can't surprise: anything explicit wins, only encoding changes (never CSV/semantics), and the behavior is visible. |
| New | How to make the auto-selection *visible*? | One **terse stderr** line on the implicit path only, prefixed via `ZSV_USAGE_PROG`, naming format + cause + override/silence, suppressible via `ZSV_AGENT_NOTICE=off` (U11a/b). e.g. `<prog>: TOON output (AI_AGENT); override: --json; silence: ZSV_AGENT_NOTICE=off`. | Direct cure for spooky-action: the magic announces itself on a channel that never corrupts stdout. Bounded (~10 tok, once/process, off-switch) so it can't erode the savings for stderr-capturing agents. |
| New | Where in the stack to build it? | **zsv first, lq remainder** (§6a, L1–L5). Encoder/decoder, chokepoint, flags, env/`AI_AGENT` detection → open zsv layer (`ZSV_*` vars); lq only wires proprietary commands through the shared writer + adds lq-only help topics. | lq is built on zsv, which already owns the JSON writer and the base emitters. Building low = one change covers all zsv-native commands, open-source review, standalone `zsv` benefits, no proprietary serialization fork. |
| New | How to keep parse reliability high? | Ship `lq help toon` documenting the grammar (R8). | Self-description. The agent loads the grammar on demand and parses reliably, instead of guessing — this is the mitigation for the one real risk in the proposal. |

## Why mirror-surface over a single `--format` flag (re-affirmed)

A lone `--format json|toon` looks simpler but (a) cannot name the converter
*commands* (`2json`/`2toon` are commands, not flags), forcing an inconsistent
surface, and (b) its only real advantage — "one thing to learn" — is better
delivered by `ZSV_STRUCTURED_FORMAT`, which covers *all* commands including
converters. Mirror siblings + env default dominates either option alone for an
agent. A `--format` alias may still be added internally; it is non-blocking
(DF1).

## Layering note (zsv vs lq) and env-var naming

Because the JSON writer, the writer abstraction, and the base JSON-emitting
commands all live in the open-source **zsv** layer (this repo) and lq is built
on top, the mechanism is implemented in zsv and the environment variables are
`ZSV_*` (matching existing precedent like `ZSV_COMPARE_OUTPUT_TYPE_JSON`), not
`LQ_*`. lq MAY add `LQ_*` aliases for brand continuity, but `ZSV_*` is
authoritative. This is the corrected naming versus the first draft, which had
used `LQ_*` before the layering was settled.

## Assumptions made while working autonomously

- A1. ~~The deliverable is a **specification for the lq dev team**, not an
  implementation in lq's C sources.~~ **Superseded:** the spec was subsequently
  implemented in the open **zsv** layer on branch `agent` (Phase 1 + core of
  Phase 2). The encoder/decoder are the installed **`libjson2toon`** reference
  library (not reimplemented); zsv adds the chokepoint wrapper, the `2toon` /
  `toon2json` commands, `compare --toon*`, the `ZSV_*`/`AI_AGENT` resolution
  chain, `help toon`, and a round-trip test. See `toon-output.md` Appendix C for
  the status and the lq-proprietary remainder (§6a/L3).
- A2. TOON remains opt-in and JSON remains the default at every level
  (flag, env unset, built-in). Non-negotiable for backward compatibility.
- A3. The installed `json2toon` is treated as the reference encoder; the spec
  pins a TOON spec version separately (§9) and `json2toon` is the conformance
  oracle, not the normative definition.
- A4. The token table in the spec is from one synthetic 200-row data corpus plus
  the live fixed help surfaces. Data-surface percentages will shift with real
  data shape; the fixed-surface numbers are stable. Stated as such in the spec.
- A5. Losslessness is currently asserted from structural inspection; the spec
  makes the in-tree `toon2json` decoder + round-trip test the mechanism that
  converts the assertion into a verified guarantee (G2/R1/§10).
- A6. Placement: `docs/toon-output.md` (+ this file) in the `zsv` repo, matching
  the existing `docs/` feature-doc convention (e.g. `compare-redline-*.md`).
  Added on branch `agent`; **not committed** — left for the team to review.

## Residual caveat (unchanged, deliberately surfaced)

Token count is a proxy for ergonomics. The spec's R8 (`lq help toon`) and §10
round-trip tests address the two ways the proxy could mislead — parse
reliability and silent data loss — but a final go/no-go should still include a
small extraction-accuracy spot-check on TOON vs JSON with the target model.
