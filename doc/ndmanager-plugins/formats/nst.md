# `.nst.<method>.N` — noise statistics

The background noise of group `N` in the waveform space of the matching
[`.spk`](spk.md): the mean window and the covariance of random windows drawn
clear of every spike, cut exactly like the `.spk` rows (same geometry, same
transform, same int16 rounding), with outliers rejected. A clusterer whitens
spike features with it. Written by
[`ndm_extractspikes`](../commands/spike-detection.md#noise-statistics-noisewindows-noiserejectz-noiseguard).
The format is called **NSTE**.

## Layout

Binary, little-endian. The single reader/writer pair is
`neurosuite::core::loadNoiseStats` / `writeNoiseStats`
(`src/libneurosuite-core/src/neurosuite/core/noise_stats.hpp`), which also holds
the estimator (`estimateNoiseStats`); `neurofileio` does not handle `.nst`.

Header, `int32` fields:

| # | Field | Meaning |
|---|---|---|
| 0 | magic | `0x4E535445` (`"NSTE"`) |
| 1 | version | `1` |
| 2 | `nSamples` | samples per window (`S`), as in the `.spk` |
| 3 | `nChannels` | channels per window (`C`), as in the `.spk` |
| 4 | `peakSampleIndex` | 0-based index of the window's reference sample |
| 5 | `guard` | clearance (samples) added around every spike window when drawing |
| 6 | `passes` | rejection passes applied |
| 7 | reserved | `0` |

Then `int64` counts — `nRequested`, `nDrawn`, `nKept`, `nRejectedFlat`,
`nRejectedOutlier` — and two `float64` — `zMax` (the rejection gate) and the
draw `seed`. Body, with `D = S × C`:

```
C × float64          robust sigma per channel (1.4826 MAD, kept windows)
D × float64          mean window
D × D × float64      covariance of the kept windows (n − 1), row-major
```

Window vectors use the `.spk` row layout, sample-major: element `t × C + c` is
sample `t` of channel `c`. A clusterer whose features are a sample range of the
window (a mask) takes the matching sub-block of the mean and covariance.

## How it is made

- Times are drawn uniformly at random, seeded by the group number, and kept when
  no spike of the group's `.res` lies within `S + guard` samples, so neither
  window touches the other; the window and the stderiv seed sample lie inside
  the recording. Draws continue until `nRequested` distinct times or the try
  budget (50 × `nRequested`) is spent; `nDrawn < nRequested` means the recording
  has too little spike-free time.
- A window is rejected when a channel is **flat** for the whole window (gap,
  stuck line); when any sample lies more than `zMax` robust SDs from its
  channel median (**amplitude**); or when its energy
  `Σ ((x − med_c) / sigma_c)²` lies more than `zMax` robust SDs above the median
  energy (**energy**). Amplitude and energy are re-tested `passes` times, each
  time with the scale of the surviving set.

## Naming

`.nst` is **MethodSpecific** in the [naming convention](naming.md):
`SESSION.nst.<method>.N`, resolved strictly — the noise of one transform is not
the noise of another. Each `.spk.<method>.N` written by `ndm_extractspikes` has
an `.nst.<method>.N` beside it.

## Writers and readers

- **Written** by [`ndm_extractspikes`](../commands/spike-detection.md) (the
  engines' `-R -N`).
- **Read** by fiber-kit (`fiber-session --whiten-source nst`).

Re-extracting the waveforms (another `.res`, another transform) makes the
`.nst` stale with the `.spk`; `ndm_extractspikes --force` rewrites both.

## See also

- [`.spk`](spk.md) — the waveforms whose noise it describes.
- [`.pca`](pca.md) — the other per-group basis file of a waveform space.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
