# `.res.<method>.N` — spike times

One timestamp per spike of spike group `N`: the sample index of the spike peak
in the `.fil` (acquisition sampling rate, 0-based). Every per-spike file of the
group — [`.spk`](spk.md), [`.fet`](fet.md), [`.clu`](clu.md), [`.clc`](clc.md)
of every method — has one row per `.res` entry, in the same order.

## Layout

Binary, no header:

```
nSpikes × int64   little-endian, sample index of the peak
fileSize = nSpikes × 8
```

- Times are in ascending order.
- The waveform of spike `i` in the `.spk` starts at sample
  `res[i] − (peakSampleIndex − 1)` of the source file (`peakSampleIndex` is
  1-based, from the group's `spikeDetection` entry).
- A plain-text form (one decimal integer per line) is also accepted by the
  `neurofileio` readers that auto-detect (NeuroScope, the template generator,
  `process_initeap`). The pipeline writes binary only, and the plugins that
  read `.res` (`process_extractspikes* -R`, `process_mergefeatures`,
  `process_spikefilter.py`) and Klusters read binary only.

The auto-detection (`neurofileio::isBinaryClusterRes`) treats a file as binary
when its size is a non-zero multiple of 8 **and** its first byte is not an ASCII
digit. Text `.res` files written for those readers (e.g. Plexon's
[`.pxres`](pxspikes.md)) must not hold negative times.

## Naming

`.res` is a **Shared** type in the [naming convention](naming.md): there is one
spike-time file per group, whatever method detected it.

- **Written** as `SESSION.res.<method>.N` under the detection method token
  (e.g. `.res.stderiv.N`).
- **Read** with `resolveAny` (`ndm_resolve_any`, `resolve_any`,
  `custody::resolveAny`): the preferred method, then `standard`, `stderiv`,
  `sdiff`, then any other `SESSION.res.<token>.N`, then the untagged
  `SESSION.res.N`. A step running under `stderiv_C5` therefore finds the
  `.res.stderiv.N` written by detection.

NeuroScope does not use the resolver: for a cluster file `X.clu.<…>` it reads
the `.res` whose name is the same with `clu` replaced by `res`.

## Writers

| Writer | When |
|---|---|
| `ndm_detectspikes` (`process_extractspikes*`) | detection; writes `.res.<method>.N`, skipping groups whose file exists |
| [`ndm_reextractspikes`](../commands/ndm_reextractspikes.md) (`process_shadowcluster`) | merges second-pass spikes into the group's files; originals kept as `.bak` |
| `ndm_extractspikes` post-extraction filter (`process_spikefilter.py`) | removes dropped rows from the shared `.res` and every row-aligned sibling; the original is kept as `<res>.prefilter` |
| KiloKlustaKwik, Klusters | after realignment / time-shift operations that move spikes |

`ndm_extractemg` writes EMG event times in the same layout as
`SESSION.res.emg.<probeId>`.

## Readers

`ndm_extractspikes` (re-extraction at existing times), `ndm_pca` (appended as
the last `.fet` column by `process_mergefeatures`), Klusters, NeuroScope, the
template and EAP tools.

## See also

- [Spike detection](../commands/spike-detection.md)
- [`.spk`](spk.md) — the waveforms cut at these times.
- [`.dat` / `.fil`](dat.md) — the signal the times index.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
