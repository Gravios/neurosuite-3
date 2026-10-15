# Spike alignment

Two commands that move spike windows so each waveform sits on its true peak.
`ndm_alignspikes` works per spike, before PCA. `ndm_alignspikes_pca` works per
cluster, after clustering. Both re-extract shifted windows from the raw
recording instead of rolling the stored snippet, and both rewrite the spike
times and waveforms in place.

**Contents:** [`ndm_alignspikes` — per-spike peak alignment before PCA](#ndm_alignspikes--per-spike-peak-alignment-before-pca) · [`ndm_alignspikes_pca` — per-cluster realignment after clustering](#ndm_alignspikes_pca--per-cluster-realignment-after-clustering) · [Shared spike times](#shared-spike-times)

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
- `stderiv_C4` / `stderiv_C5` pass the group's `sdiffPairs` to the aligner as
  `-P`. A group without `sdiffPairs` is skipped with a warning. A token whose
  order disagrees with the pattern (`_C4` with a reference-set pattern) is
  refused.

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

## `ndm_alignspikes_pca` — per-cluster realignment after clustering

### Synopsis

```bash
ndm_alignspikes_pca [session.yaml] [electrodeGroup] [--method M] [-- process_alignspikes_pca flags]
```

**Input:** `session.fil` (or `session.dat` when there is no `.fil`), `session.clu.<method>.N`, `session.res.<method>.N`, `session.spk.<method>.N`, `session.pca.<method>.N` (for stage 2)

**Output:** `session.spk.<method>.N`, `session.res.<method>.N`, `session.clu.<method>.N` rewritten in place; `session.res.<method>.N.preAlignPca` (times before this run); `session.alignspikes_pca.log.N` (per-cluster CSV); stale `session.fet.<method>.N` renamed to `.fet.<method>.N.preAlignPca`

### Description

Runs on an existing clustering and realigns each cluster in two stages:

1. **Per spike.** Each spike is aligned to its cluster's mean waveform by
   normalised cross-correlation over lags `±maxShift`. Spikes with a non-zero
   best lag and a score ≥ `minScore` are re-extracted from the raw file at the
   corrected time. The mean is recomputed and the pass repeated, up to
   `iterations` times or until nothing moves.
2. **Per cluster.** The cluster mean is evaluated at every rigid shift
   `s ∈ [−maxShiftGlobal, +maxShiftGlobal]` (each spike re-extracted and
   averaged), and its energy in the group's PCA basis is computed:

   ```
   E(s) = Σ_ch Σ_k ⟨φ_ch,k, μ(s) − μ̄_ch⟩²
   ```

   The shift that maximises `E` is applied to every spike in the cluster.
   Because all members move by the same amount, a cluster that is a mixture of
   several units keeps its internal spread. A per-spike PCA refinement would
   pull those units onto one position.

Re-extraction follows the same rules as `ndm_alignspikes`: the extractor's
arithmetic, the `.spk`'s own seed convention, and no window that starts before
sample 1 or runs past the end of the raw file (such a spike keeps its previous
position).

Clusters 0 and 1 (noise and artefact) are not touched, and clusters with fewer
than 5 spikes are skipped. Stage 2 is skipped for a group whose
`.pca.<method>.N` is missing or cannot be read (a warning is printed), and for
every group when `skipPca` is true.

After the shifts the spike times are re-sorted, and `.res`, `.clu` and `.spk`
are rewritten in that order. The `.fet` no longer matches the waveforms: the
script renames it to `.fet.<method>.N.preAlignPca`. The `.pca` basis is left in
place. Regenerate features with
[`ndm_refeaturize`](pca.md#ndm_refeaturize--re-project-onto-the-saved-basis-fet)
(keeps the basis) or [`ndm_pca`](pca.md#ndm_pca--per-channel-pca-spk--fet-pca)
(refits it). One or two further passes of `ndm_pca` + `ndm_alignspikes_pca`
can refine the result.

```
ndm_pca → ndm_klustakwik → ndm_alignspikes_pca → ndm_refeaturize (or ndm_pca)
```

The backup `session.res.<method>.N.preAlignPca` is overwritten on every run.
After a second pass it holds the times from the first pass, not the original
detection times.

Without an `electrodeGroup` argument, all groups are processed one after
another (each binary uses OpenMP internally). With one, only that group runs.
A group whose `.clu`, `.res` or `.spk` is missing is skipped. The `.clu` is
looked up strictly as `.clu.<method>.N`; `.res` and `.spk` fall back to the
`standard` and untagged names. Group geometry (`channels`, `nSamples`,
`peakSampleIndex`) and `nChannels` come from the session YAML.

**Methods.** `standard`, `sdiff` and stderiv tokens are accepted. Stderiv
re-extraction applies the all-pairs spatial derivative, so `stderiv_S1`,
`stderiv_S2` and the custom-pattern tokens `stderiv_C4` / `stderiv_C5` are
refused by `process_alignspikes_pca`. Unlike `ndm_alignspikes`, the script does
not pass the group's `sdiffPairs`.

**Extra flags.** Anything after the positional arguments (or after `--`) is
passed to `process_alignspikes_pca`:

| Flag | Effect |
|---|---|
| `--dry-run` | Compute the shifts and write the log without modifying `.spk` / `.res` / `.clu`. The script still renames the `.fet`. |
| `--log PATH` | Write the CSV log to PATH instead of `session.alignspikes_pca.log.N`. With all groups, every group writes to the same file, so use it with a single group. |
| `-v`, `--verbose` | Print a line for every cluster. |
| `--skip-pca` | Same as `skipPca: true`. |

**Log.** One CSV row per processed cluster, with the columns
`cluster, nSpikes, n_stage1_shifted, median_score, stage2_global_shift,
stage2_pca_gain_pct, n_total_shifted, frac_total_shifted`.

### Parameters

```yaml
- name: ndm_alignspikes_pca
  parameters:
    - name: maxShift
      value: 3
      status: Optional
    - name: maxShiftGlobal
      value: 4
      status: Optional
    - name: iterations
      value: 2
      status: Optional
    - name: minScore
      value: 0.70
      status: Optional
    - name: minSpikes
      value: 5
      status: Optional
    - name: skipPca
      value: 'false'
      status: Optional
    - name: method
      value: standard
      status: Optional
```

| Name | Default | Meaning |
|---|---|---|
| `maxShift` | 3 | Stage-1 per-spike shift bound, samples. |
| `maxShiftGlobal` | 4 | Stage-2 rigid cluster shift bound, samples. |
| `iterations` | 2 | Maximum stage-1 passes per cluster. |
| `minScore` | 0.70 | Normalised cross-correlation score a spike needs before stage 1 shifts it. |
| `minSpikes` | 5 | Intended minimum cluster size. **Currently not used:** the script reads it but does not pass it on, and the binary always skips clusters with fewer than 5 spikes. |
| `skipPca` | `false` | `true` / `yes` / `1` runs stage 1 only. |
| `method` | `standard` | Method token of the files to read and rewrite. `--method` on the command line overrides it. |

All parameters follow the usual [parameters](../parameters.md) lookup.

### Exit status

0 when done; 11 when the session file is missing or neither `session.fil` nor
`session.dat` exists; 1 for an out-of-range `electrodeGroup` or missing
`nChannels` / group geometry. When `process_alignspikes_pca` fails, its status
is returned (2 for its own errors). Groups skipped for missing inputs do not
change the status.

---

## Shared spike times

`.res` is method-independent: one file per group serves every method (see
[naming](../formats/naming.md#artifact-classes)). Both commands rewrite it, so the
corrected times are used by every later extraction. Because the spikes are also
re-sorted by time, any **other** method's `.spk`, `.fet` or `.clu` already
written for the same group no longer lines up row-for-row with the new `.res`.
Re-extract those methods from the aligned `.res` instead of reusing their old
files.

## See also

- [PCA feature extraction](pca.md) — `ndm_pca`, `ndm_refeaturize`
- [Spike detection](spike-detection.md) — extraction, and [custom difference patterns](spike-detection.md#custom-difference-patterns-and-alignment)
- [`ndm_klustakwik`](ndm_klustakwik.md)
- [`.res`](../formats/res.md), [`.spk`](../formats/spk.md), [`.clu`](../formats/clu.md), [`.pca`](../formats/pca.md), [`.fet`](../formats/fet.md) formats
- [File naming](../formats/naming.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
