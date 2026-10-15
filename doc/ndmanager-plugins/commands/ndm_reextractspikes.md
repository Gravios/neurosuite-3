# `ndm_reextractspikes` — masked second-pass detection with in-place shadow clustering (`.fil` → `.res`/`.spk`/`.fet`/`.clu`)

## Synopsis

```bash
ndm_reextractspikes [session.yaml] [--method M] [--methodOrder N]
```

**Input:** `session.fil`; per group `session.res.<method>.N` (resolved across
methods), `session.spk.<method>.N`, `session.fet.<method>.N`,
`session.clu.<method>.N`, `session.pca.<method>.N`

**Output:** per merged group, the input `.res`, `.spk.<method>.N`,
`.fet.<method>.N` and `.clu.<method>.N` rewritten in place; a
`session.shadowmark.<method>.N` sidecar; `<file>.bak` backups of the four
originals

## Description

Finds low-amplitude spikes the first detection pass missed and merges them
into the existing sort.

1. **Re-detection (Pass 1).** The engine scans `session.fil` at a reduced
   threshold and rejects every candidate peak within ±`reextractMaskHalfWidth`
   samples of an existing spike time, so only missed spikes survive.
2. **Backup.** On the first merge of a group, each original is copied to
   `<file>.bak`; an existing `.bak` is kept, not overwritten.
3. **Shadow clustering (Pass 2).** `process_shadowcluster` projects each new
   waveform through the group's PCA basis and assigns it by robust
   Mahalanobis distance (per-dimension median and MAD × 1.4826) to the
   nearest eligible parent cluster, or to one "unmatched" bin when it fails
   the χ² test for every parent. New spikes get new cluster IDs; existing
   IDs are untouched:

   ```
   shadow(k) = maxExistingId + 1 + k                    (paired with cluster k)
   unmatched = maxExistingId + 1 + maxExistingId + 1
   ```

   In Klusters, merge `shadow(k)` into `k`, keep it as a separate unit, or
   reject it. Re-cluster the unmatched bin with
   [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md).

Groups without a `.res` (any method) or without the method's `.spk`, `.fet`
or `.clu` are skipped with a warning; if no group qualifies the script exits
11. A group where Pass 1 finds no new spikes is left unchanged. The
`.pca.<method>.N` is not checked up front, but shadow clustering cannot run
without it: a missing basis stops the run with an error at that group (groups
already merged stay merged).

### Method

`method` (`standard` | `sdiff` | `stderiv`, or a suffixed stderiv token such
as `stderiv_S2`) must match the detection / PCA / clustering
run being continued: it selects the `.<type>.<method>.N` names and the
detection engine.

- `standard`, `sdiff` — `process_reextractspikes`. Per-channel thresholds are
  `process_medianthreshold` baselines over `[start, start+duration)`
  multiplied by `reextractThresholdFactor`. `sdiff` waveforms are raw, so it
  shares this engine; only the file names differ.
- `stderiv` — `process_reextractspikes_stderiv`. Detection runs on the
  spatial + temporal first-difference signal of order `methodOrder`; the
  threshold is `reextractThresholdFactor` × the noise level the engine
  computes over `[start, start+duration)`. New waveforms are written in
  stderiv space, matching the reference rows and the `pca.stderiv.N` basis.
  A suffixed token (`stderiv_S<n>`) fixes the order and overrides
  `methodOrder`. `stderiv_C<n>` tokens pass the token's order but not the
  group's `sdiffPairs` pattern to the engine.

Spike times are method-independent: the group's `.res` is found under the
requested method first, then `standard`, `stderiv`, `sdiff`, any other tag,
and finally the untagged name, and the merged times are written back to that
same file. Other methods' `.spk`/`.fet`/`.clu` for the group are not updated
and no longer line up with it.

### Transactional safety

Pass 2 writes to a sandbox stem (`session-reextr-merge.*`), reading the live
files only through temporary raw-named links (`session-reextr-ref.*`). Each
output is verified present and non-empty before the commit, which is a set of
atomic renames onto the dotted names, `.clu` last. If shadow clustering fails,
the live files are untouched and the script exits with an error. To roll a
group back, rename the `.bak` files over the live ones (and delete the
`.shadowmark`).

## Parameters

Set in the `ndm_reextractspikes` block (see [parameters](../parameters.md)):

| Name | Default | Meaning |
|---|---|---|
| `reextractThresholdFactor` | `0.75` | Threshold factor for re-detection, used **instead of** `thresholdFactor` (baseline × factor). If left empty: 0.75 × `thresholdFactor`. |
| `reextractMaskHalfWidth` | empty | Minimum distance in samples from every existing spike time. Empty: the largest `nSamples` of the active groups. |
| `reextractMinClusterSize` | `50` | Parent clusters with fewer spikes are not eligible for shadow assignment. |
| `reextractChi2` | `0.999` | χ² quantile for the squared-Mahalanobis admission threshold; lower is stricter (more spikes go to the unmatched bin). |
| `reextractExtraFeat` | `auto` | Whether the reference `.fet` carries the per-channel peak features of `process_pca -x`: `auto` (inferred from the dimension count), `0` or `1`. |
| `reextractInflationRatio` | `5.0` | Cap on new spikes a parent may absorb, as a multiple of its size; excess spikes (largest d²) go to the unmatched bin. `0` disables. |
| `reextractInflationCap` | `500` | Minimum per-parent cap: the limit is `max(reextractInflationCap, reextractInflationRatio × parent size)`. `0` disables. |
| `reextractAutoSubcluster` | `0` | `1`: run `ndm_subcluster_unmatched` on the session after a successful merge. It uses the `method` of its own block (`--method` is not passed on), so set the same method there. A failure is only a warning. |
| `method` | `standard` | Engine and file names (see above). `--method` overrides it. |
| `methodOrder` | `3` | Spatial-derivative order 1–3 for `sdiff`/`stderiv`. `--methodOrder` overrides it; `sdiffOrder` is read as a fallback name. |

`thresholdFactor`, `refractoryPeriod`, `peakSearchLength`, `start` and
`duration` are mandatory; they are read from this block and fall back to the
[`ndm_detectspikes`](spike-detection.md) block. `refractoryPeriod` and
`peakSearchLength` are passed to the engine; `start`/`duration` set the
threshold window. `thresholdFactor` itself is only used for the empty-factor
default above.

Per group, `channels`, `nSamples` and `peakSampleIndex` come from
`spikeDetection.channelGroups`. The engine applies one waveform length and
peak index to all groups: if a group differs from group 1, a warning is
printed and group 1's values are used.

## `.shadowmark` sidecar

`session.shadowmark.<method>.N` is a small text file, one `key value` pair per
line:

```
unmatched_id 23
shadow_offset 12
n_unmatched 4021
n_new 4410
```

`ndm_subcluster_unmatched` processes only groups that have it and takes the
unmatched bin ID from it. Do not delete it between the two steps; it is
regenerated only by re-running `ndm_reextractspikes`.

## See also

- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) — re-cluster the unmatched bin
- [`ndm_redetectspikes`](ndm_redetectspikes.md) — re-detection on the spike-stripped residual instead of a mask
- [Re-extract with a lower threshold](../../workflows/re-extract-lower-threshold.md) — workflow recipe
- [`reextractspikes-v1`](../../design/reextractspikes-v1.md), [`reextract-v2`](../../design/reextract-v2.md) — design notes
- [File naming](../formats/naming.md), [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
