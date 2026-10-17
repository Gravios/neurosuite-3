# File naming — the variant (chain-of-custody) convention

Every per-group spike-sorting file in neurosuite-3 carries an explicit
**method** (variant) token in its name. Klusters, NeuroScope, KiloKlustaKwik,
the `ndm_*` plugins and fiber-kit all follow this one convention.

The policy lives in **`custody.hpp`**
(`src/libneurosuite-core/src/neurosuite/core/custody.hpp`). It has two mirrors,
**`ndm_custody`** (bash, sourced by `ndm_functions`) and
**`ndm_resolve_io.py`** (Python). All three are tested against the same
conformance vectors (`src/libneurosuite-core/test/custody_vectors.tsv`). Call
sites do not decide strict-vs-fallback resolution themselves.

**Contents:** [Naming model](#naming-model) ·
[Artifact classes](#artifact-classes) ·
[Resolution rules](#resolution-rules) · [Method tokens](#method-tokens) ·
[Reading a name](#reading-a-name) ·
[Files outside the type tables](#files-outside-the-type-tables) ·
[EAP template-library family](#eap-template-library-family-composed-directly) ·
[Derivation and staleness](#derivation-and-staleness) ·
[Backward compatibility](#backward-compatibility)

## Naming model

```
<base>.<type>.<method>.<group>[.<suffix>]   per-group, method-tagged   (written by the pipeline)
<base>.<type>.<group>[.<suffix>]            per-group, untagged        (legacy; still read)
<base>.<type>                               session-wide               (dat, fil, lfp, yaml, …)
```

- **`base`** — the session base name. It may contain dots: the parser looks
  for the *last* known `type` token, not the first dot.
- **`type`** — the artifact type (`res`, `spk`, `fet`, `clu`, …).
- **`method`** — the variant token, `<family>[_<kind><order>][_D<lag><dims>]`;
  see [Method tokens](#method-tokens). Absent in an untagged legacy name.
- **`group`** — the spike-group index (`1`, `2`, …), all digits. A token after
  the type that is not all digits is the method; the first all-digit token is
  the group.
- **`suffix`** *(optional)* — tokens after the group, e.g. a stage or
  experiment tag: `rat01.clu.stderiv.5.stack` has suffix `stack`. Composed
  with `stagePath(base, type, method, group, stage)`.

Group 5 of session `rat01`:

| Retired name | Current name |
|---|---|
| `rat01.fet.5` | `rat01.fet.standard.5` |
| `rat01.fetD.5` | `rat01.fet.stderiv.5` |
| `rat01.pcaD.5` | `rat01.pca.stderiv.5` |
| `rat01.clu.5` | `rat01.clu.standard.5` |
| `rat01.spkD.5` | `rat01.spk.stderiv.5` |

## Artifact classes

Each known type belongs to exactly one class, which fixes how it is resolved
(`custody::classify`, `ndm_classify`, `classify`):

| Class | Types | Name | Resolution |
|---|---|---|---|
| **MethodSpecific** | `clu`, `clc`, `fet`, `pca`, `col`, `model`, `klg` | `<base>.<type>.<method>.<group>` | **Strict** — exactly that path, no fallback. |
| **Shared** | `res`, `spk` | `<base>.<type>.<method>.<group>` | First existing of a fixed candidate list (below). |
| **SessionWide** | `fil`, `dat`, `xml`, `yaml`, `nrs`, `par`, `eeg`, `lfp` | `<base>.<type>` | No method, no group. |

Any type not listed as Shared or SessionWide is classed MethodSpecific, but
only the types in the table are *known* to the parser (`isKnownType`); see
[Files outside the type tables](#files-outside-the-type-tables).

**Why `clu`/`fet`/`pca`/… are strict.** A missing method-specific file must be
an error, not a silent switch to another variant: a `stderiv` `.clu` read with a
`standard` `.fet` is a correctness bug.

**Why `res` is Shared.** Spike times do not depend on the waveform method.
Detection may run under one token and extraction, PCA and sorting under
another, but there is one `.res` per group: `ndm_detectspikes` writes it as
`.res.<detect-method>.N`, and every later step finds it with `resolveAny`
(below). See [`.res`](res.md).

**`spk` is Shared but carries a domain.** The extractor writes the waveforms in
its own transform domain (`stderiv`: spatial + temporal derivative; `sdiff`:
spatial derivative; `standard`: raw), so `.spk.stderiv.N` and `.spk.standard.N`
hold different data. Because `spk` is Shared, a request for a missing
`.spk.stderiv.N` falls back to `.spk.standard.N` and returns **raw** waveforms.
A caller that needs a specific domain checks the method of the file actually
resolved (`resolvedIsStderiv`). See [`.spk`](spk.md).

## Resolution rules

`resolve(base, type, group, method)` (C++ `custody::resolve`, bash
`ndm_resolve`, Python `resolve`):

- **SessionWide** → `<base>.<type>`.
- **MethodSpecific** → `<base>.<type>.<method>.<group>`, whether or not it
  exists.
- **Shared** → the first existing of:
  1. `<base>.<type>.<method>.<group>`
  2. `<base>.<type>.<waveform-token>.<group>` — only when `method` carries a
     `_D<lag><dims>` suffix (`stderiv_C5_D34` → `stderiv_C5`);
  3. `<base>.<type>.standard.<group>` — only when `method` ≠ `standard`;
  4. `<base>.<type>.<group>` (untagged legacy).

  If none exists, the result is the path under the waveform token (step 2's
  name, or step 1's when there is no `_D` suffix), for writers and error
  messages.

  Step 2 exists because `_D` names a *feature space* computed over the waveforms
  of the token without it; there is no separate `_D` waveform file. Klusters
  opened on `.clu.stderiv_C5_D34.N` therefore reads `.spk.stderiv_C5.N`, and a
  realign or Save As writes back to that file.

**`resolveAny(base, type, group, preferred)`** — for a Shared file whose
writing token is not known (C++ `custody::resolveAny`, bash `ndm_resolve_any`,
Python `resolve_any`). Order:

1. `<base>.<type>.<preferred>.<group>` (if `preferred` is given);
2. `.standard`, `.stderiv`, `.sdiff` (each once);
3. any other `<base>.<type>.<token>.<group>` in the directory, where `<token>`
   contains no dot;
4. the untagged `<base>.<type>.<group>`.

If none exists it returns `<base>.<type>.<preferred or standard>.<group>`.
Every `.res` consumer in the pipeline uses this: `ndm_extractspikes`, `ndm_pca`,
Klusters (`resolveFeatureAny`).

Step 3 accepts *any* token, so a per-group file with the same type and group
number but unrelated content is also a candidate. Bash and Python take the
lexically first match; C++ takes the first in directory-iteration order. Keep
foreign files out of the `.res.<token>.N` namespace — Plexon's online spikes are
written as `.pxres.N` for this reason ([`.pxres`](pxspikes.md)).

## Method tokens

A method token is `<family>[_<kind><order>][_D<lag><dims>]`. Suffixes are
parsed from the right, so a token may carry both.

| Part | Values | Meaning |
|---|---|---|
| family | `standard`, `sdiff`, `stderiv` | the extraction engine and waveform domain |
| kind | `S`, `C` | `S`: plain `methodOrder` spatial derivative (any `sdiffPairs` deliberately unused). `C`: the session's custom `sdiffPairs` pattern. |
| order | integer | the spatial-derivative order actually applied |
| `_D<lag><dims>` | one digit each | lagged PCA feature space: per channel PC1 at −lag/0/+lag samples, plus PC2 when dims is 4 |

Examples: `stderiv_S3` (plain all-pairs), `stderiv_C4` (single-partner
pattern), `stderiv_C5` (reference-set pattern), `stderiv_C6` (expression
pattern), `stderiv_C5_D34` (lag-3,
4-column feature space over the `stderiv_C5` waveforms, written by
[`ndm_pca`](../commands/pca.md) or fiber-kit). The waveform token of a `_D`
token is the token without the suffix (`custody::waveformToken`,
`ndm_waveform_token`, `waveform_token`).

Rules, enforced by `ndm_check_method_token` (bash) before any wrapper runs:

- The family must be `standard`, `sdiff` or `stderiv`.
- The `_S`/`_C` suffix applies to `stderiv` only.
- `_S` takes order 1–3; `_C` takes order 4 (single-partner pattern) or 5
  (reference-set pattern). `stderiv_S4` and `stderiv_C3` are refused.
- Orders 4 and 5 come from the pattern itself (`+` present ⇒ 5, else 4); they
  cannot be requested through `methodOrder`. A `_C` token whose digit
  disagrees with the session's `sdiffPairs` is refused.
- A token that does not match the grammar is **opaque**: the whole string is
  the family, so an unknown token is never mistaken for a known one.

`isStderivMethod` / `ndm_is_stderiv_method` / `is_stderiv_method` is true for
the whole stderiv family — `stderiv` and any parsed `stderiv_*` token. It is not
a prefix test: `stderivfoo` is a different (opaque) family.

The point of recording kind and order in the *name*: a consumer knows how a file
was produced without re-reading session parameters that may have changed since.
A bare `stderiv` carries no order, so nothing beyond the family can be inferred
from it.

## Reading a name

`parseAnchor(path)` (bash `ndm_parse_anchor`, Python `parse_anchor`) returns
`{base, type, method, group, suffix, ok}`. It scans from the right for the last
known type token (at least one base field must precede it), then reads
`[.<method>].<group>[.<suffix>]`. `methodOf(path)` returns the method of a
name ending in the group, or `""` for an untagged legacy name.

Klusters reads the method off the file you open (`.clu` or `.fet`; an untagged
name counts as `standard`) and resolves every sibling — `.spk`, `.fet`, `.pca`,
`.res` — in that method.

## Files outside the type tables

Only the types listed under [Artifact classes](#artifact-classes) are known to
the parser and resolvers. Everything else is named by its writer and read by
explicit path:

| Files | Named | Notes |
|---|---|---|
| [`.clp`](clp.md) | `<base>.clp.<method>.<group>[.<suffix>]` | Klusters derives it from the opened `.clu` name by swapping the type token. |
| [`.cli`](cli.md) | `<base>.cli.<method>.<group>[.<suffix>]` | Same rule as `.clp`. |
| [`.pxres`, `.pxclu`, `.pxspk`](pxspikes.md) | `<base>.<type>.<group>` | Deliberately outside the tables so they never stand in for pipeline files. |
| [`.px<stream>`](pxstream.md), `.emgclean`, `.<ext>.slice.<tag>` | session-wide by convention | Raw signal files read through `inputExtension`; see [`.dat`](dat.md). |
| [`.evt`](evt.md) | `<base>.<id>.evt` or `<base>.evt.<id>` | NeuroScope convention. |
| `.res.emg.<id>`, `.spk.emg.<id>` | type `res`/`spk`, method `emg`, group = probe id | Written by `ndm_extractemg`; they parse as Shared files and are visible to `resolveAny` (see the warning above). |

## EAP template-library family (composed directly)

The EAP template-class layer — [`.eap`](eap.md), [`.tcl`](tcl.md),
[`.wti`](wti.md), [`.wtf`](wtf.md) — is not in `custody.hpp`'s type tables, so it
never goes through `resolve()`. Writers and readers compose these paths with
`untaggedPath` / `stagePath`; a fixed per-group layer always exists, so there is
nothing to fall back to.

| Type | Composed as | Method | Stage |
|---|---|---|---|
| `.eap` | `untaggedPath(base,"eap",group)` → `<base>.eap.<group>` | none | per stage (trailing `.<stage>` appended) |
| `.tcl` | `untaggedPath(base,"tcl",group)` → `<base>.tcl.<group>` | none | one per session and group |
| `.wti` | `untaggedPath(base,"wti",group)` → `<base>.wti.<group>` | none | per stage (trailing `.<stage>` appended) |
| `.wtf` | `stagePath(base,"wtf",variant,group,stage)` → `<base>.wtf.<variant>.<group>[.<stage>]` | tagged | per stage |

Membership, the class registry and the template index do not depend on the
waveform method, so `.eap`/`.tcl`/`.wti` carry no method token. Only the `.wtf`
waveform stack does, because its sample values depend on the extraction method.
`.tcl` takes no stage because a class id must mean the same column at every
stage.

## Derivation and staleness

Realigning waveforms invalidates the features computed from them, the `.fet`
and the `.pca`. `ndm_alignspikes` renames them to `.prealign`, so `ndm_pca`
must be run again.

## Backward compatibility

- **Untagged names** (`<base>.<type>.<group>`) are recognised everywhere: the
  parser yields `method = ""` (treated as `standard`), and for Shared types the
  untagged file is the last fallback. MethodSpecific types never fall back to
  it.
- **Glued names** (`.fetD.N`, `.spkD.N`, `.pcaD.N`) are not part of the custody
  model. A few legacy readers still probe them: `resolve_input` in
  `process_autoklusta.py` and `ndm_resolve_input` in `ndm_functions`.
  (`neurofileio::resolveInput` did too until it was removed on 2026-10-17.)
  `process_pca` invoked directly with a `.fetD.N` output name writes `.pcaD.N`.
- New files are always written method-tagged.

## See also

- [`.res`](res.md), [`.spk`](spk.md), [`.fet`](fet.md), [`.pca`](pca.md),
  [`.clu`](clu.md) — the per-group files this convention names.
- [`ndm_functions`](../commands/ndm_functions.md) — the bash helpers that source
  `ndm_custody`.
- [Parameters](../parameters.md) — setting `method` for a step.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
