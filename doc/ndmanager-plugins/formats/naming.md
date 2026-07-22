# File naming — the variant (chain-of-custody) convention

Every per-group artifact in neurosuite-3 is named with an explicit
**method** (a.k.a. **variant**) tag. This is the single naming convention
shared by all programs — Klusters, Neuroscope, the `ndm_*` plugins, and
fiber-kit — and it replaces the older `.spk`/`.spkD`, `.fet`/`.fetD`,
`.pca`/`.pcaD` "D-suffix" scheme.

The policy lives in one place, **`custody.hpp`**
(`src/libneurosuite-core/src/neurosuite/core/custody.hpp`), with byte-exact
mirrors in bash (`ndm_custody`) and Python (`ndm_resolve_io.py`) kept in
lock-step by shared conformance vectors (`custody_vectors.tsv`). No call
site decides strict-vs-fallback resolution by hand.

## Naming model

```
<base>.<type>.<method>.<group>          per-group, method-tagged   (canonical)
<base>.<type>.<group>                   per-group, untagged        (legacy)
<base>.<type>                           session-wide               (fil/dat/…)
```

- **`base`** — the session base name. May itself contain dots; the parser
  finds the *last* known `type` token rather than the first dot.
- **`type`** — the artifact type (`spk`, `fet`, `clu`, …).
- **`method`** — the variant token, `<family>[_<kind><order>]`; see
  [Method tokens](#method-tokens). Absent in untagged legacy names.
- **`group`** — the electrode-group index (`1`, `2`, …). All-digits; this
  is how the parser tells a `method` token from the `group`.
- **`suffix`** *(optional)* — trailing post-group tokens after the group,
  e.g. an experiment tag `session.clu.stderiv.5.stack` → suffix `stack`.

Example, group 5 of session `rat01`:

| Old (retired) | New |
|---|---|
| `rat01.fet.5` | `rat01.fet.standard.5` |
| `rat01.fetD.5` | `rat01.fet.stderiv.5` |
| `rat01.pcaD.5` | `rat01.pca.stderiv.5` |
| `rat01.clu.5` | `rat01.clu.standard.5` |
| `rat01.spkD.5` | `rat01.spk.stderiv.5` |

## Artifact classes

Each type belongs to exactly one class, which fixes how it is resolved:

| Class | Types | Naming | Resolution |
|---|---|---|---|
| **MethodSpecific** | `clu`, `clc`, `fet`, `pca`, `col`, `model`, `klg` | `<base>.<type>.<method>.<group>` | **Strict** — exactly that path, no fallback. |
| **Shared** | `res`, `spk` | one physical copy across methods | Prefer `<method>`, then `standard`, then the untagged legacy path; first that exists. |
| **SessionWide** | `fil`, `dat`, `xml`, `yaml`, `nrs`, `par`, `eeg`, `lfp` | `<base>.<type>` | No method, no group. |

**Why `res` is Shared.** Spike *times* are method-independent: detection may
run under one token while extraction, alignment and sorting run under another,
and there is exactly **one `.res` per group** whatever wrote it. Resolve it
with `resolveAny` (below), not `resolve` — the latter walks only
method → standard → untagged and misses another token's copy.

**`spk` is Shared but domain-carrying.** `process_extractspikes_stderiv` writes
the **transformed** waveform to `.spk.<method>.<group>` at full group width;
the transform is *not* deferred to PCA time. A stderiv session therefore has
its own `.spk`, distinct from a standard one, and the `SDIFF_PASS` reduction at
PCA time operates on that already-transformed file. Because `spk` is classed
Shared, a request that misses can fall back to `standard` — handing back **raw**
waveforms for a stderiv request. Callers needing a specific domain should check
the resolved method (`resolvedIsStderiv`) rather than trust the fallback.

**Why `clu`/`fet`/… are strict.** A method-specific file that does not
exist should be an error, not a silent fall-back to a different variant's
data — mixing a `stderiv` `.clu` with a `standard` `.fet` would be a
correctness bug.

## Resolution rules

Given a `base`, `type`, `group`, and requested `method`:

- **SessionWide** → `<base>.<type>` (exists or not).
- **MethodSpecific** → `<base>.<type>.<method>.<group>` (exists or not; no
  fallback).
- **Shared** → the first of these that exists, else the method-tagged path
  (for the error message):
  1. `<base>.<type>.<method>.<group>`
  2. `<base>.<type>.standard.<group>` *(only if `method` ≠ `standard`)*
  3. `<base>.<type>.<group>` *(untagged legacy)*

**`resolveAny()` — shared artifacts whose writing token is unknown.**
`resolve()` cannot find a copy written under a *third* method, and no fixed
list can enumerate suffixed tokens. `resolveAny()` tries the preferred method,
then `standard`/`stderiv`/`sdiff`, then **any** other method-tagged copy in the
directory, then the untagged legacy name. Use it for `.res`. Available as
`custody::resolveAny` (C++), `ndm_resolve_any` (bash), `resolve_any` (Python).

`resolvedIsStderiv()` is true for the whole stderiv **family** — `stderiv` and
any `stderiv_*` token. Deliberately not a `"stderiv"` prefix test, so
`stderivfoo` is a different family.

## Method tokens

A method is `<family>[_<kind><order>]`:

| Part | Values | Meaning |
|---|---|---|
| family | `standard`, `sdiff`, `stderiv` | the engine |
| kind | `S`, `C` | `S` = plain `methodOrder` spatial derivative; `C` = the session's custom `sdiffPairs` pattern |
| order | integer | the spatial-derivative order actually applied |

Examples: `stderiv_S3` (plain all-pairs, any `sdiffPairs` deliberately unused),
`stderiv_C4` (custom single-partner pattern), `stderiv_C5` (custom
reference-set pattern).

Rules, enforced by `ndm_check_method_token` and its C++/Python mirrors:

- The suffix applies to `stderiv` only.
- `_S` takes order 1–3; `_C` takes order 4 (single-partner) or 5
  (reference-set). `stderiv_S4` and `stderiv_C3` are refused as incoherent.
- Orders 4 and 5 are **derived from the pattern's own grammar** (`+` present ⇒
  5, else 4) and cannot be requested through `methodOrder`.
- A token not matching the grammar is **opaque**: the whole string becomes the
  family, so an unknown token never masquerades as a known one.

Recording kind and order in the *name* is the point — a consumer knows how a
file was produced without re-reading session parameters that may have changed
since. A bare `stderiv` carries no order, so nothing beyond the family can be
inferred from it.

## Derivation and staleness

Realigning waveforms invalidates the position/feature files derived from
them: `staleAfterRealign()` = `{fet, pca}`. Wrappers archive those after a
realign rather than hand-rolling the list.

## Reading a name

`parseAnchor(path)` returns `{base, type, method, group, suffix, ok}` by
scanning from the right for the last known `type` token, then
`[.<method>].<group>[.<suffix>]`. `methodOf(path)` returns just the method
(`""` for an untagged legacy name). Programs use the parsed `method` as the
session's variant — e.g. Klusters reads the method off the `.clu` you open
and resolves every sibling (`spk`, `fet`, `pca`, …) in that variant.

## Backward compatibility

Untagged legacy names (`<base>.<type>.<group>`) are still recognised: for
Shared types they are the last fallback, and for the parser an untagged
name simply yields `method = ""` (treated as `standard`). New files are
written method-tagged.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
