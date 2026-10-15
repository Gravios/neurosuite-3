# `.spk.<method>.N` — spike waveforms

The waveform snippet of every spike of group `N`, one record per
[`.res`](res.md) entry, in the transform domain of the method that extracted it.

## Layout

Binary, no header, little-endian:

```
nSpikes records, each nSamples × nChannels int16
value(spike s, sample t, channel c)  at  int16 index  (s × nSamples + t) × nChannels + c
fileSize = nSpikes × nSamples × nChannels × 2
```

- **Order:** sample-major within a record (all channels of sample 0, then
  sample 1, …), channel fastest.
- **Geometry** is not stored: `nSamples` and `peakSampleIndex` come from the
  group's `spikeDetection` entry in the session YAML, `nChannels` is the number
  of channels in the group (in group order). Readers derive `nSpikes` from the
  file size and reject a size that is not a whole number of records.
- **Window:** record `i` covers source samples
  `[res[i] − (peakSampleIndex − 1), … + nSamples)`; the peak is at 1-based
  position `peakSampleIndex` in the record.
- Klusters also reads 4-byte samples when the session's `nBits` is 32; no
  pipeline step writes them.

## Transform domain

The method's family decides what the samples are. `ndm_extractspikes`
re-extracts at the shared `.res` times (`-R`) and writes:

| Family | Samples in `.spk.<method>.N` |
|---|---|
| `standard` | raw `.fil` values |
| `sdiff` | spatial derivative across the group's channels (no temporal difference) |
| `stderiv` | spatial derivative, then temporal first difference — the clustering space |

All three keep the full group width. For `stderiv`, the channel reduction that
drops a linearly dependent channel happens later, inside
[`ndm_pca`](../commands/pca.md) (`process_pca_stderiv -d 4`), not in the file.
The derivative order and pattern are given by the method token (`_S<n>`,
`_C4`, `_C5`) or, for a bare token, by `methodOrder` / `sdiffPairs`; see
[method tokens](naming.md#method-tokens).

So `.spk.standard.N` and `.spk.stderiv.N` hold different data and are not
interchangeable.

## Naming

`.spk` is a **Shared** type in the [naming convention](naming.md), written as
`SESSION.spk.<method>.N`. A lookup for a method whose file does not exist falls
back to its waveform token (for a `_D` feature-space token), then
`.spk.standard.N`, then the untagged `.spk.N`. That fallback can return raw
waveforms for a stderiv request: callers that need a specific domain check the
method of the file actually resolved (`resolvedIsStderiv`). A `_D` token never
has a `.spk` of its own; `.clu.stderiv_C5_D34.N` reads `.spk.stderiv_C5.N`.

## Writers

| Writer | Notes |
|---|---|
| `ndm_extractspikes` | `.spk.<method>.N` from `SESSION.<inputExtension>` (default `.fil`) at the shared `.res`; skips groups whose file exists unless `--force` |
| `ndm_extractspikes` post-extraction filter (`process_spikefilter.py`) | drops rows from every row-aligned per-spike file of the group |
| [`ndm_reextractspikes`](../commands/ndm_reextractspikes.md) | merges second-pass spikes into the group's files |
| Klusters | rewrites the loaded `.spk` on realign / Save |

The detection engines also produce a `.spk`, which `ndm_detectspikes`
discards. `ndm_extractemg` writes EMG event waveforms in the same layout as
`SESSION.spk.emg.<probeId>`.

## Readers

[`ndm_pca`](../commands/pca.md) (`process_pca`), Klusters (waveform view,
realignment), the template generator and fiber-kit. [`.wtf`](wtf.md) template
stacks use the same record layout and the same reader.

## See also

- [Spike detection](../commands/spike-detection.md) — engines and options.
- [`.res`](res.md), [`.fet`](fet.md), [`.pca`](pca.md).

---

*Part of the [ndmanager-plugins](../README.md) reference.*
