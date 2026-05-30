# Iterative refinement (strip → redetect → re-sort)

Subtracts curated cluster waveforms from the raw signal, runs spike
detection again on the cleaned trace, and re-sorts. The result is
often noticeably cleaner separation between units that overlap in
feature space — particularly useful when two units share a channel
and their waveforms partially cancel during detection.

Use this *after* a first round of curation, not before — the strip
needs templates from labelled clusters.

## Steps

### 1. Strip curated waveforms from the raw trace

```sh
ndm_stripdat session.yaml
```

Three subtraction modes:

| Mode | When to use |
|---|---|
| `model` (default) | Per-cluster template with projection-based amplitude scaling and burst-shape compensation. The right default for most cases. |
| `raw` | Subtracts raw `.spk.N` waveforms directly. Use when you don't yet have a curated `.clu.N`. |
| `botm` | Bayes-Optimal Template Matching. ~100× better residuals than `model` on the synthetic test fixture. Use when LFP analysis is the goal. |

Mode is chosen via the YAML:

```yaml
- name: ndm_stripdat
  parameters:
  - {name: subtractionMode,  value: 'botm', status: Optional}
  - {name: autoStrip,        value: 'true', status: Optional}
  - {name: autoStripQuality, value: '1,2,3', status: Optional}
```

`autoStripQuality` controls which quality-annotation tiers feed the
strip — `'1,2,3'` strips Uncertain through Good (skip Bad). The
original `.dat` is **never modified**; a new `session-spkclean.dat`
is written.

For the BOTM derivation and a side-by-side comparison, see
[`../design/modeling-l1-vs-botm.md`](../design/modeling-l1-vs-botm.md).
For the full command reference, see
[`ndm_stripdat`](../ndmanager-plugins/commands/ndm_stripdat.md).

### 2. Re-detect on the cleaned trace

```sh
ndm_redetectspikes session.yaml
```

Runs the full spike detection chain (`ndm_hipass` →
`ndm_extractspikes`) on `session-spkclean.dat`, then merges newly
detected spikes into the existing `.res` / `.spk` / `.clu` files via
`process_mergespikes`. Deletes `.fet.N` since the spike order has
changed.

### 3. Rebuild features and re-sort

```sh
ndm_pca_stderiv  session.yaml      # → fresh .fetD.N, .pcaD.N
ndm_klustakwik   session.yaml      # → updated .clu.N
```

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
klusters session.fet.1
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
