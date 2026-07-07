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
In the stderiv method the transform is applied downstream at PCA time (`ndm_pca --method stderiv`), not stored as a separate waveform file; the `.fet.stderiv.N` it produces contains
the stderiv-space waveform directly, not the raw signal. This is the
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



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
