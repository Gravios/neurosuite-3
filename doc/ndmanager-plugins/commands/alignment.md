# Spike alignment

`ndm_alignspikes` moves each spike window so the waveform sits on its true peak.
It works per spike, before PCA, re-extracts the shifted windows from the raw
recording instead of rolling the stored snippet, and rewrites the spike times and
waveforms in place.

Realignment within a cluster, after sorting, is not a pipeline step: use
Klusters (Realign Spikes) or fiber-kit (`fiber-realign`). The step that did it
here, `ndm_alignspikes_pca`, was removed on 2026-11-24.

**Contents:** [`ndm_alignspikes` — per-spike peak alignment before PCA](#ndm_alignspikes--per-spike-peak-alignment-before-pca) · [Shared spike times](#shared-spike-times)

---

## `ndm_alignspikes` — per-spike peak alignment before PCA

### Synopsis

```bash
ndm_alignspikes [session.yaml] [--method M]
```

**Input:** `session.fil`, `session.res.<method>.N` (shared spike times, any method tag), `session.spk.<method>.N`

**Output:** `session.spk.<method>.N` and `session.res.<method>.N` rewritten in place; `session.res.<method>.N.prealign` (original times, first run only); stale `session.fet.<method>.N` / `session.pca.<method>.N` renamed to `*.prealign`

### Description

The detector places each spike window on a threshold crossing. The true peak
can sit a few samples to either side, and that offset varies from spike to
spike. If it does, PCA sees several shifted copies of one neuron's waveform as
separate clouds, and KlustaKwik tends to split the unit into two or three
clusters. `ndm_alignspikes` measures the offset δ for each spike and moves the
window onto the peak.

Pipeline position:

```
ndm_extractspikes → ndm_alignspikes → ndm_pca → ndm_klustakwik
```

For each electrode group the script calls `process_alignspikes`, which:

1. Computes the cross-channel energy `E(t) = Σ_c |W[t,c]|` of each spike
   (over the `topChannels` highest-amplitude channels when that is set).
2. Finds δ with one of two methods, selected by `alignSigma`:
   - `alignSigma ≤ 0`: the argmax of `E` within
     `peakSampleIndex ± maxShift`, refined to sub-sample precision with a
     parabola. For the stderiv family the window is re-read from `.fil` and
     given the spatial operator only (no temporal difference), because the
     temporal first difference puts an energy zero at the peak.
   - `alignSigma > 0`: circular cross-correlation of `E` over the full
     `nSamples` window against a Gaussian of standard deviation `alignSigma`
     centred on `peakSampleIndex`. This works on the stored `.spk` snippet
     without reading `.fil`. A σ of about 1.5 or more bridges the two energy
     lobes of a stderiv waveform.
3. Leaves spikes whose relative energy score is below `minScore` unshifted.
4. Re-extracts every shifted spike from `.fil` at `originalTs + δ` with exactly
   the arithmetic of `ndm_extractspikes`, applying the same spatial transform the
   `.spk` was built with. For the stderiv family the temporal difference of
   each window is seeded the way the existing `.spk` was written — from the
   sample before the window (`ndm_extractspikes`) or with 0 (detection-pass and
   older `.spk` files); the convention is detected from the file and printed,
   and an undetermined file is treated as seeded from the previous sample.
   A shift whose window would start before sample 1 or run past the end of
   `.fil` is not applied (`ndm_extractspikes` writes no row for such a window,
   so the aligned `.res` could not be re-extracted row for row); the summary
   counts these as `edge-refused`.
5. Writes the `.spk` sorted by the new times, and rewrites `.res` so that
   `.res[i] = originalTs[i] + δ[i]`. This keeps the invariant that the peak of
   `.spk[i]` is at file sample `.res[i]`, which Klusters' nudge and any tool that
   re-reads `.fil` at `.res` offsets depend on. The original `.res` is copied to
   `<res>.prealign` once; a later run does not overwrite that archive.

The script then renames `session.fet.<method>.N` and `session.pca.<method>.N`
to `*.prealign`. **Re-run [`ndm_pca`](pca.md#ndm_pca--per-channel-pca-spk--fet-pca)
before sorting.**

**Methods.** `method` must name the pipeline that produced the `.spk`
(`standard`, `sdiff`, `stderiv`, `stderiv_S<order>`, `stderiv_C<order>`; see
[method tokens](../formats/naming.md#method-tokens)). An invalid token is an
error. The aligner can only reproduce all-pairs (order 3) or the group's custom
pattern, so:

- `stderiv_S1` and `stderiv_S2` are refused (first difference and Laplacian
  are not implemented here).
- `stderiv_C4` / `stderiv_C5` / `stderiv_C6` pass the group's `sdiffPairs` to the aligner as
  `-P`. A group without `sdiffPairs` is skipped with a warning. A token whose
  order disagrees with the pattern (`_C4` with a reference-set pattern) is
  refused.
- a bare `stderiv` passes the group's `sdiffPairs` when the group defines one,
  because that is how `ndm_extractspikes` extracted it; `stderiv_S3` never
  passes it. This is the same rule `ndm_extractspikes` and
  `ndm_reextractspikes` use.

See [custom difference patterns and alignment](spike-detection.md#custom-difference-patterns-and-alignment).

**Row count.** The `.spk` must have one row per `.res` time. If it is short
by whole rows, the usual cause is that `ndm_extractspikes` skipped spikes whose
window starts before sample 1 or runs past the end of `.fil`; the aligner stops
and says so. Remove those times (or re-detect) and re-extract first.

**File resolution.** The `.res` is found across all method tags (detection may
have run under a different token). The `.spk` is resolved as a shared artifact:
`.spk.<method>.N`, then `.spk.standard.N`, then the untagged `.spk.N`. A group
with no `.spk` or no `.res` is skipped with a warning; the other groups are still
processed. The step always runs; existing outputs do not cause a skip.

Group geometry comes from the session YAML: `channels`, `nSamples` and
`peakSampleIndex` of each group, and `acquisitionSystem/nChannels`. The raw data
file is always `session.fil`.

### Parameters

```yaml
- name: ndm_alignspikes
  parameters:
    - name: maxShift
      value: 3
      status: Optional
    - name: minScore
      value: 0.0
      status: Optional
    - name: method
      value: standard
      status: Optional
    - name: topChannels
      value: 0
      status: Optional
    - name: alignSigma
      value: 0.0
      status: Optional
```

| Name | Default | Meaning |
|---|---|---|
| `maxShift` | 3 | Largest shift in samples, either direction. Must be > 0 and < `nSamples`. Values above 5 risk pulling in spikes from neighbouring units (3 samples = 94 µs at 32 kHz). |
| `minScore` | 0.0 | Minimum relative energy score in [0, 1] (peak energy / window energy) for a shift to be applied. Spikes below it stay put. 0 accepts every shift. |
| `topChannels` | 0 | Compute the energy on the N highest-amplitude channels of each spike only, so collisions or residual activity on other channels do not pull the peak. 0 = all channels. |
| `alignSigma` | 0.0 | σ in samples of the Gaussian template. > 0 selects circular cross-correlation over the full window; ≤ 0 selects the bounded argmax. |
| `method` | `standard` | Method token of the `.spk` to align. `--method` on the command line overrides it. |

All parameters follow the usual [parameters](../parameters.md) lookup.

### Exit status

0 when done (including groups skipped for missing files); 11 when the session
file or `session.fil` is missing; 1 for an invalid method token, for missing
`nChannels` or group geometry (`channels` / `nSamples` / `peakSampleIndex`), or
when `process_alignspikes` fails on a group.

---

## Shared spike times

`.res` is method-independent: one file per group serves every method (see
[naming](../formats/naming.md#artifact-classes)). `ndm_alignspikes` rewrites it, so the
corrected times are used by every later extraction. Because the spikes are also
re-sorted by time, any **other** method's `.spk`, `.fet` or `.clu` already
written for the same group no longer lines up row-for-row with the new `.res`.
Re-extract those methods from the aligned `.res` instead of reusing their old
files. When any spike moved, it prints a warning naming the other
`.spk` files of the group that exist (`.spk.<other method>.N` and the untagged
`.spk.N`).

## See also

- [PCA feature extraction](pca.md) — `ndm_pca`, `ndm_refeaturize`
- [Spike detection](spike-detection.md) — extraction, and [custom difference patterns](spike-detection.md#custom-difference-patterns-and-alignment)
- [`ndm_klustakwik`](ndm_klustakwik.md)
- [`.res`](../formats/res.md), [`.spk`](../formats/spk.md), [`.clu`](../formats/clu.md), [`.pca`](../formats/pca.md), [`.fet`](../formats/fet.md) formats
- [File naming](../formats/naming.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
