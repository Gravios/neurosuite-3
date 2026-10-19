# Iterative refinement (strip → redetect → re-sort)

Subtracts curated cluster waveforms from the raw signal, runs spike
detection again on the cleaned trace, and re-sorts. The result is
often noticeably cleaner separation between units that overlap in
feature space — particularly useful when two units share a channel
and their waveforms partially cancel during detection.

Use this *after* a first round of curation, not before — the strip
needs templates from labeled clusters.

> **File names.** `ndm_stripdat` and `ndm_redetectspikes` read and
> write the per-group files under their **untagged** names
> (`session.res.N`, `session.spk.N`, `session.clu.N`), not the
> method-tagged `.<type>.<method>.N` files the current
> detect/extract/sort chain writes. Provide untagged files (raw,
> `standard`-method waveforms) for every spike group before step 1;
> `ndm_stripdat` exits without writing anything if a group's `.res` or
> `.spk` is missing.

## Steps

### 1. Strip curated waveforms from the raw trace

```sh
ndm_stripdat session.yaml
```

Three subtraction modes:

| Mode | When to use |
|---|---|
| `raw` (default) | Subtracts each spike's own `.spk.N` waveform. Near-exact cancellation, no model fitting; the right choice before re-detection. |
| `model` | Per-cluster template with projection-based amplitude scaling and burst-shape compensation; preserves baseline noise. |
| `botm` | Bayes-Optimal Template Matching. ~100× better residuals than `model` on the synthetic test fixture. Use when LFP analysis is the goal. |

Mode and cluster selection are set in the YAML:

```yaml
- name: ndm_stripdat
  parameters:
  - {name: subtractionMode, value: 'botm', status: Optional}
  - {name: autoStrip,       value: 'true', status: Optional}
  - {name: qualityTags,     value: 'good,great,excellent', status: Optional}
```

Which clusters are stripped, per group: a hand-written
`session.strip.N` list (one cluster ID per line) if present; otherwise,
with `autoStrip: true` (the default), every unit in the YAML `units:`
table whose quality text matches one of `qualityTags`
(case-insensitive; default `good,great,excellent` — the quality field
of Klusters' cluster information dialog); otherwise `stripClusters`
(default: every cluster except 0 and 1). The original `.dat` is
**never modified**; a new `session-spkclean.dat` is written. An
existing `session-spkclean.dat` is not overwritten unless
`overwrite: true`.

For the BOTM derivation and a side-by-side comparison, see
[`../design/modeling-l1-vs-botm.md`](../design/modeling-l1-vs-botm.md).
For the full command reference, see
[`ndm_stripdat`](../ndmanager-plugins/commands/ndm_stripdat.md).

### 2. Re-detect on the cleaned trace

```sh
ndm_redetectspikes session.yaml
```

High-pass filters `session-spkclean.dat` with `process_medianfilter`,
sets thresholds with `process_medianthreshold` and detects and extracts
with `process_extractspikes` (the `standard` engine), using the
`ndm_hipass` and `ndm_detectspikes` parameters of the first round. It
then merges the new spikes into the untagged `session.res.N` /
`.spk.N` / `.clu.N` files via `process_mergespikes` and deletes
`session.fet.N`, since the spike order has changed.

### 3. Rebuild features and re-sort

```sh
ndm_pca        session.yaml   # → fresh feature files
ndm_klustakwik session.yaml   # → updated cluster files
```

Both commands work on the method-tagged files
(`.spk.standard.N` → `.fet.standard.N` → `.clu.standard.N`), so bring
the merged untagged `.res.N` / `.spk.N` from step 2 into those names
first.

The new spikes will join existing clusters where they fit and form
new clusters where they don't. The original curated clusters are
*not* preserved as IDs — this is a fresh sort, not an additive
re-extract. Use this approach when you want maximum clarity at the
cost of redoing curation.

For an *additive* approach that preserves curated IDs, see
[Re-extract with a lower threshold](re-extract-lower-threshold.md)
instead — that's the right workflow when you only want to add weak
units.

### 4. Curate again

```sh
klusters session.fet.standard.1
```

Most curated clusters re-emerge with similar IDs (KiloKlustaKwik tends
to produce the same partition when fed the same data). Cluster-info
annotations from the previous round are preserved in the YAML, so
you only have to re-annotate clusters that genuinely changed.

## When to use this loop

- **Yes, run iterative refinement** when curating shows two units
  whose waveforms partially overlap in time — strip-and-resort
  separates them more cleanly than splitting in feature space ever
  will.
- **Yes** when LFP analysis is downstream — the cleaned `.dat` has
  curated spikes removed, leaving the LFP-band signal intact for
  ripple/oscillation analysis.
- **No** when you're after weak-unit recovery — that's
  [re-extract with a lower threshold](re-extract-lower-threshold.md).
- **No** when first-pass detection looks clean — the strip costs
  several minutes and a curation round; not worth it on a
  well-isolated probe.

## See also

- [`ndm_stripdat`](../ndmanager-plugins/commands/ndm_stripdat.md)
- [`ndm_redetectspikes`](../ndmanager-plugins/commands/ndm_redetectspikes.md)
- [`../design/subtractspikes-botm.md`](../design/subtractspikes-botm.md) — BOTM derivation
- [`../design/modeling-l1-vs-botm.md`](../design/modeling-l1-vs-botm.md) — model comparison
