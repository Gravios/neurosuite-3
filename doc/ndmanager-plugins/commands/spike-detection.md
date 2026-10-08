# Spike detection

Short utilities for spike detection.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_extractspikes` — raw threshold detection

Detects threshold crossings on the high-pass filtered signal and extracts
raw waveform snippets from `.fil`. Writes the waveform with no transform;
this is the "traditional" pipeline.

```yaml
- name: ndm_extractspikes
  parameters:
  - {name: thresholdFactor,  value: 1.5, status: Mandatory}
  - {name: refractoryPeriod, value: 25,  status: Mandatory}
  - {name: peakSearchLength, value: 50,  status: Mandatory}
```


---

## `ndm_extractspikes_sdiff` — spatial-Laplacian detection (legacy variant)

Drop-in replacement for `ndm_extractspikes`. Applies a discrete Laplacian
across probe channels before thresholding to suppress common-mode noise,
while writing original (un-differentiated) waveforms to `.spk.N`.
Superseded in most workflows by `ndm_extractspikes_stderiv`; kept for
compatibility with earlier sessions.


---

## `ndm_extractspikes_stderiv` — spatial + temporal derivative detection

Detection runs on `stderiv[t, ch] = sdiff[t, ch] − sdiff[t−1, ch]`,
where `sdiff` is the spatial derivative across the group's channels.
In the stderiv method the transform is applied at EXTRACTION: the `.spk.<method>.N`
holds the transformed waveform at full group width, and `.fet.<method>.N` is its
projection. What `ndm_pca` does downstream is the `SDIFF_PASS` channel reduction
on that already-transformed file, not the transform itself. This is the
recommended pipeline for high-density probes: common-mode noise is
rejected twice (spatial, then temporal) and downstream PCA sees a
dramatically cleaner signal.

Four spatial orders are supported:

| Order | Name | Formula |
|---|---|---|
| `0` | SDIFF_NONE | `s[i] = x[i]` (temporal first-difference only) |
| `1` | SDIFF_FIRST | `s[i] = x[i] − x[i+1]` (nearest-neighbour) |
| `2` | SDIFF_LAPLACIAN | `s[i] = x[i] − 0.5 × (x[i-1] + x[i+1])` (discrete Laplacian) |
| `3` | SDIFF_ALLPAIRS | `s[i] = n × x[i] − Σⱼ x[j]` (default; no probe-order requirement) |

Orders 1 and 3 produce a rank-deficient transform (one linearly
dependent channel), handled automatically by `ndm_pca_stderiv`.

### Threshold noise base (`thresholdNoise`, engine `-T`)

The threshold is `thresholdFactor × 4 × σ`, with σ = median(|x|)/0.6745 over
the `start`/`duration` noise window.  `thresholdNoise` (set in the
`ndm_detectspikes` node; stderiv engine only) chooses the signal σ is measured on:

| Value | σ measured on | Threshold on the detection signal |
|---|---|---|
| `sdiff` (default) | spatial derivative only, *before* the temporal difference | higher than `factor × 4σ` — the differenced signal is quieter |
| `stderiv` | the spatial + temporal derivative actually thresholded | exactly `factor × 4σ` |

`sdiff` is the historical behaviour and stays the default so existing sessions
reproduce.  On the reference session (group 6, 36–41 min) the temporal
difference has ~0.6× the noise of the spatial derivative, so `thresholdFactor`
0.8 lands at 5.1–5.5σ of the detection signal rather than 3.2σ.  With
`stderiv`, re-pick the factor: ~1.33 reproduces the historical level (11.5k vs
12.3k spikes in that 5-minute window), 1.15 gives ~4.6σ (21.7k), 1.0 gives 4σ
(40.6k).  `ndm_reextractspikes_stderiv` already uses the `stderiv` convention.
Every run prints the effective threshold in detection-signal σ, e.g.

```
Group 6 noise base: sdiff (historical); effective threshold 5.13..5.49 sigma of the detection signal (factor 0.8 x 4 = 3.2)
```

```yaml
- name: ndm_detectspikes
  parameters:
  - {name: method,         value: stderiv, status: Optional}
  - {name: thresholdNoise, value: stderiv, status: Optional}
  - {name: thresholdFactor, value: 1.33,   status: Mandatory}
```


---

## `ndm_spikecleaner` — drop flat / railed waveforms

Examines every `spikeDetection` group and removes spike waveforms where
one or more channels are entirely flat: either all-zero (ADC cutout,
hardware disconnection) or stuck at a constant non-zero value (railed
amplifier, DAC fault). Run after any of the extraction variants and
before PCA — these degenerate waveforms would otherwise produce
degenerate feature vectors and corrupt cluster models.


---

## `ndm_denoiseuniform` — remove uniform-noise events

Removes electrically uniform events (common-mode artefacts, motion noise,
electrical interference) from `.spk.N`/`.res.N` after raw spike extraction.
Run before `ndm_pca`. Backs up originals to `SESSION_denoise_backup/`.

For the stderiv pipeline, the detection stage already rejects most
common-mode events, so this step is usually not needed.

```yaml
- name: ndm_denoiseuniform
  parameters:
  - {name: uniformityThreshold, value: 0.30, status: Optional}
  - {name: removeFlat,          value: 1,    status: Optional}
  - {name: dryRun,              value: 0,    status: Optional}
```




## Custom difference patterns and alignment

A spike group may carry an `sdiffPairs` pattern (group-local, 0-based) that
replaces the plain spatial derivative. Its grammar fixes the order: `a-b,c-d,…`
is a single-partner map (order 4), `a-b+c+d,…` is a per-channel reference set
(order 5). Select it with a `_C` method token — `stderiv_C4` or `stderiv_C5` —
which is refused if it disagrees with what the pattern implies.

Orders 4 and 5 are **not** selectable through `methodOrder`: the engines'
`-d` accepts 0–3 only, and `-d` must never be passed alongside `-P` (it is
range-checked first and exits before `-P` is read). `stderiv_S<order>` runs the
plain derivative and leaves any `sdiffPairs` deliberately unused.

`ndm_alignspikes` re-extracts from `.fil`, so it must apply the *same* spatial
operator the `.spk` was built with; it passes the group's pattern through `-P`.
A `_C` method with no pattern available is refused rather than silently aligned
with all-pairs. `_S1`/`_S2` are also refused — first-difference and Laplacian
are not implemented in the aligners.

Alignment rewrites `.res` so `.res[i]` marks the true peak, archiving the
original as `.res.<g>.prealign`. Because `.res` is method-independent, the
corrected times serve every later extraction.

---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
