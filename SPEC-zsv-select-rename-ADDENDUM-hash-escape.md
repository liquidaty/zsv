# SPEC ADDENDUM — `lq select --rename`: addressing columns whose name begins with `#`

**Parent spec:** `SPEC-lq-select-rename.md` (F1, shipped & verified in `lq 20260629a`).
**Status:** ready to implement. **Type:** backward-compatible grammar extension to the `--rename` selector parser.
**Acceptance form:** *fixture → command → expected observable result*, runnable offline (harness: `f1-rename-tests.make`, AC14–AC18).

---

## 1. Problem

The shipped `--rename` selector grammar overloads `#` as an unconditional "index" sigil:

```
selector := name | "#" index
```

The instant the parser sees a leading `#` it commits to index form, so a column whose **literal header begins with `#`** is unreachable by name. The parent spec acknowledged this as an "accepted, documented limitation" (§3.2) and noted the fallback would be a paired `--rename-index` form (§6.1). Verified against `20260629a`:

| Selector | Header | Current result | Problem |
|---|---|---|---|
| `#foo=x` | `id,#foo,amt` | error: `invalid column index in: #foo=x` | a real `#foo` column can't be renamed; the error misdiagnoses the cause |
| `\#foo=x` | `id,#foo,amt` | error: `no column named "\#foo"` | no escape exists — the backslash is taken as part of the name |
| `#5=NEW` | `id,#5,amt` | error: `column index 5 out of range` | bare `#5` is (correctly) an index, so it can never reach a column *named* `#5` |

The limitation is small in frequency but absolute in effect: there is **no** way, by name or by escape, to target a `#`-prefixed column. This addendum closes it.

## 2. Fix — narrow the index rule, add a one-character escape

Two changes to the selector parser only (no other `lq select` surface is affected — see §6):

1. **`#` denotes an index only when it is followed by digits** (the whole token, up to the split `=`). `#` followed by anything else is a literal **name**.
2. **`\` escapes the next character.** A leading `\#` is therefore a literal `#`; `\\` is a literal `\`. This is the *only* way to address the one genuinely ambiguous shape — a column whose literal name is `#`+digits (e.g. `#5`).

The argument continues to split on the first `=` (now: the first **unescaped** `=`), preserving the parent spec's "newname may contain `=`" property and additionally letting an escaped `\=` appear in a *selector* name if ever needed.

### 2.1 Amended selector grammar (replaces parent §3.2 grammar block)

```
selector := index | name
index    := '#' DIGITS                 # 1-based input column position (e.g. #2, #10)
name     := literal header text, with these escapes recognized at parse time:
              \#  → a literal '#'      (needed only to start a name with '#')
              \\  → a literal '\'
              \=  → a literal '='      (optional; lets a selector name contain '=')
```

### 2.2 Resolution order (normative)

A `--rename` selector resolves by this precedence:

1. If, after the first unescaped `=` is located, the selector text is `#` followed by **one or more digits and nothing else** → **index** (1-based input position).
2. Otherwise → **literal name**, after applying the `\#` / `\\` / `\=` un-escaping. Matches **every** input column with that exact name (unchanged name semantics).

| Selector | Resolves to | Notes |
|---|---|---|
| `#2` | index 2 | unchanged — every shipped AC (AC2/3/4/6/7) is byte-identical |
| `#foo` | name `#foo` | `#`+non-digit can't be an index ⇒ zero ambiguity ⇒ no escape required |
| `\#foo` | name `#foo` | escape is harmless/optional here (equals `#foo`) |
| `\#5` | name `#5` | **the disambiguator** — literal `#5`, *not* index 5 |
| `\\srv` | name `\srv` | literal leading backslash |
| `name` | name `name` | unchanged |

The escape is **only ever required** for the pathological `#`+digits *name*. Every ordinary `#`-prefixed header (`#ref`, `#id`, `#foo`) resolves with no escape at all.

## 3. Amended §5 edge-case matrix (append these rows)

| Case | Input header | `--rename` arg | Expected output header |
|---|---|---|---|
| `#`-name, escaped | `id,#foo,amt` | `\#foo=x` | `id,x,amt` |
| `#`-name, unescaped (auto) | `id,#foo,amt` | `#foo=x` | `id,x,amt` |
| `#digits`-name needs escape | `id,#5,amt` | `\#5=NEW` | `id,NEW,amt` |
| bare `#digits` stays an index | `id,name,amt` | `#2=b` | `id,b,amt` |
| bare `#digits` ignores same-named col | `id,#5,amt` | `#5=oops` | error: index 5 out of range (3 cols) |

## 4. Amended §3.5 errors

- An escaped/name selector that matches no column → error naming the **resolved literal name** (e.g. `no column named "#5"`), **not** the raw selector (`\#5`) — so the message shows what was actually searched for.
- Drop the misleading `invalid column index in: #foo` path: `#`+non-digit is no longer an attempted index, so it can never produce that message. A genuine bad index (`#9` on a 2-column file) keeps its existing `index 9 out of range (input has N columns)` message.

## 5. Backward compatibility

This is a **pure superset**. Every selector that is valid today behaves identically:

- All shipped `#N` usages (`#2`, `#3`, …) are `#`+digits ⇒ still indexes. AC1–AC13 unchanged; AC17/AC18 below are explicit regression guards.
- The only changed behavior is that selectors which **previously errored** now *succeed*: `#foo` (was `invalid column index`) now resolves to the name `#foo`. A previously-erroring input doing something sensible is strictly an improvement; no previously-succeeding input changes.
- `\#…` previously matched a literal-backslash name (a name that essentially never exists); redefining it as an escape reclaims dead syntax.

## 6. Scope / blast radius

The `#`-as-index sigil exists **only inside `--rename`**. `lq select`'s other column references — the `--` projection list and `-x <name>` — switch to index mode via the separate `-n` flag and have no in-band `#` sigil, so they already address a `#`-named column literally and need **no** change. The fix is one parser, one selector tokenizer.

## 7. Decision record — escape character

**Chosen: backslash (`\`).** Rationale:

- Universal "escape next char" metaphor; an agent/engineer recognizes it instantly and it generalizes for free (`\=`, `\\`).
- In a **makefile** recipe (this harness is one), `\#` is already make's own native way to emit a literal `#`, so the convention composes rather than fights the surrounding tool.
- **Rejected — doubled sigil `##`:** ad-hoc, doesn't generalize to `=`, and still doesn't rescue make's `#`-comment handling.
- **Rejected — separate `--rename-index N=new` flag (the parent §6.1 fallback):** would make `--rename`'s LHS always-a-name and move the index form out of band — a **breaking** change to the already-shipped `#N`-in-band grammar (AC2/3/4/6/7) and the skill/eval that now teach it. Not acceptable post-ship.

## 8. New acceptance criteria (AC14–AC18; in `f1-rename-tests.make`)

Fixtures are generated inline by the harness.

- **AC14 (escaped `#`-name).** `lq select --rename '\#foo=x'` on `id,#foo,amt` → header `id,x,amt`.
- **AC15 (unescaped `#`+non-digit auto-resolves to name).** `lq select --rename '#foo=x'` on `id,#foo,amt` → `id,x,amt` (no longer `invalid column index`).
- **AC16 (escape disambiguates `#digits`-name).** `lq select --rename '\#5=NEW'` on `id,#5,amt` → `id,NEW,amt` (the **name** `#5`, not index 5).
- **AC17 (regression: bare `#digits` is still an index).** `lq select --rename '#2=b'` on `id,name,amount` → `id,b,amount`.
- **AC18 (regression: bare `#5` is an index, ignores a same-named column).** `lq select --rename '#5=oops'` on the 3-column `id,#5,amt` → non-zero exit, index-5-out-of-range error.

AC17/AC18 must stay green at all times (they prove the shipped behavior is untouched). AC14–AC16 are red against `20260629a` until the parser change lands.
