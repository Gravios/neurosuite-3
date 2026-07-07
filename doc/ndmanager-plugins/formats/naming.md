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
- **`method`** — the variant. `standard` is the default; `stderiv` is the
  spatial-derivative / temporal-difference domain. Absent in untagged
  legacy names.
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
| `rat01.spkD.5` | *(no separate file — see Shared below)* |

## Artifact classes

Each type belongs to exactly one class, which fixes how it is resolved:

| Class | Types | Naming | Resolution |
|---|---|---|---|
| **MethodSpecific** | `clu`, `clc`, `fet`, `pca`, `col`, `model`, `klg` | `<base>.<type>.<method>.<group>` | **Strict** — exactly that path, no fallback. |
| **Shared** | `res`, `spk` | one physical copy across methods | Prefer `<method>`, then `standard`, then the untagged legacy path; first that exists. |
| **SessionWide** | `fil`, `dat`, `xml`, `yaml`, `nrs`, `par`, `eeg`, `lfp` | `<base>.<type>` | No method, no group. |

**Why `res`/`spk` are Shared.** Spike *times* are method-independent, and
the raw waveform snippets are shared too: the stderiv transform is applied
**downstream at PCA time** (producing `fet.stderiv`), not stored as a
second `.spk`. So a stderiv session reads the same raw `.spk` as a standard
one. A resolved file reports the `method` of the copy actually found, so a
`.spk` that fell back to the raw/untagged path is **not** treated as
stderiv even in a stderiv session.

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

`resolvedIsStderiv()` is true iff the resolved file's method is `stderiv`.

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
