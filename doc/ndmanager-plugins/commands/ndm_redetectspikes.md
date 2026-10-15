# `ndm_redetectspikes` — second-round detection on the spike-stripped recording (`-spkclean.dat` → `.res`/`.spk`/`.clu`)

## Synopsis

```bash
ndm_redetectspikes [session.yaml]
```

**Input:** `session-spkclean.dat` (from [`ndm_stripdat`](ndm_stripdat.md));
per group `session.res.N`, `session.spk.N`, optionally `session.clu.N`

**Output:** `session.res.N`, `session.spk.N`, `session.clu.N` rewritten with
the new spikes merged in; `session.fet.N` deleted

## Description

Finds spikes that were hidden under larger, already-sorted spikes and adds
them to the existing sort:

1. High-pass filters `session-spkclean.dat` with the median filter
   (`process_medianfilter`, half-window `windowHalfLength`) into a temporary
   `session-spkclean.fil`. This is always the median filter, whatever
   `filter` the `ndm_hipass` block selects.
2. Computes per-channel thresholds with `process_medianthreshold` over
   `[start, start+duration)` of that file, multiplied by `thresholdFactor`,
   and detects and extracts spikes with `process_extractspikes` (the
   `standard` engine).
3. Merges the new spikes into each group's files with `process_mergespikes`:
   new spikes get cluster ID 0, the merged set is sorted by time, and a new
   spike on exactly the same sample as an existing one is dropped. `.res`,
   `.spk` and `.clu` are rewritten in place through temporary files; a group
   without a `.clu` gets cluster 0 for every spike. The group's `.fet` is
   deleted, since spike count and order have changed.

The temporary `.fil` and per-group detection files are removed;
`session-spkclean.dat` is kept. If no group gains a spike, the script reports
it and exits 0.

All per-group files use the **untagged** names (`session.res.N`,
`session.spk.N`, `session.clu.N`, `session.fet.N`); method-tagged
`.<type>.<method>.N` files are neither read nor updated. The `.clu` is read
and written as text. The detection engine applies group 1's `nSamples` and
`peakSampleIndex` to every group.

Re-run [`ndm_pca`](pca.md) and [`ndm_klustakwik`](ndm_klustakwik.md) (or sort
in Klusters) afterwards. For a non-destructive alternative that keeps the
existing cluster IDs and features and works on the original `.fil`, see
[`ndm_reextractspikes`](ndm_reextractspikes.md).

## Parameters

The `ndm_redetectspikes` block has no parameters of its own in the template.
Every value is read from elsewhere (see [parameters](../parameters.md)):

| Name | Read from | Meaning |
|---|---|---|
| `thresholdFactor` | this block, else `ndm_detectspikes` | Threshold = baseline noise × factor. Mandatory. |
| `refractoryPeriod` | this block, else `ndm_detectspikes` | Samples skipped after each detection. Mandatory. |
| `peakSearchLength` | this block, else `ndm_detectspikes` | Samples searched for the peak. Mandatory. |
| `start`, `duration` | this block, else `ndm_detectspikes` | Window (s) used to estimate the thresholds. Mandatory. |
| `memCapGB` | this block, else `ndm_detectspikes` | Memory cap for `process_extractspikes`; default 2. |
| `windowHalfLength` | `ndm_hipass` (or `general`) | Median-filter half-window in samples. Mandatory: the template leaves it commented out in `ndm_hipass`, so set it there. |
| `chunkSize` | `ndm_hipass` | Filter chunk size in bytes; default 134217728, capped at 512 MiB. |

A missing mandatory value stops the script with an error (exit 1).
`nChannels`, `nBits` and `samplingRate` come from `acquisitionSystem`;
`channels`, `nSamples` and `peakSampleIndex` from
`spikeDetection.channelGroups`.

## Typical workflow

```bash
ndm_klustakwik session.yaml      # initial sort
# (Klusters — curate)
ndm_stripdat session.yaml        # subtract trusted clusters → session-spkclean.dat
ndm_redetectspikes session.yaml  # detect missed spikes, merge as cluster 0
ndm_pca session.yaml             # rebuild features
ndm_klustakwik session.yaml      # re-sort the merged set
```

`ndm_stripdat` with `redetectSpikes: yes` runs this step automatically.

## See also

- [`ndm_stripdat`](ndm_stripdat.md) — produces the cleaned `.dat`
- [`ndm_reextractspikes`](ndm_reextractspikes.md) — masked re-detection on the original `.fil` with shadow clustering
- [Iterative refinement](../../workflows/iterative-refinement.md)
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
