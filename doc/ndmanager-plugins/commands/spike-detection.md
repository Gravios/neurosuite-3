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

### Re-centring (`recenterMode`, `recenterHalfWidth`; engine `-C`, `-W`)

After detection, each spike is moved to a refined position and that position
is what the shared `.res` records (`ndm_extractspikes -R` later extracts exactly
there; it does not re-centre).  Before 2026-10-08 the engine persisted the
refined positions only when re-centring reordered a group's spikes or a spike
was rejected, so older `.res` files may hold un-re-centred detection times for
some groups.  Both settings live in the `ndm_detectspikes`
node and apply to the stderiv engine only.

| Setting | Meaning |
|---|---|
| `recenterMode: stderiv` (default) | sample of maximum Σ\|stderiv\| over the group |
| `recenterMode: raw` | sample of maximum negative deflection of the group-referenced raw signal (x − group mean), Σ over the group in per-channel noise units — the physical trough |
| `recenterHalfWidth: N` | search ±N samples around the detection; `0` = no re-centring |
| `recenterHalfWidth` unset | historical window `[−(peakSampleIndex−1), peakSearchLength−peakSampleIndex]` |

The historical window is wide (−20…+19 samples at `peakSearchLength` 40): on
the reference chunk it moves 9.8% of detections by more than 5 samples and 3.4%
by more than 10 — far enough to land on a neighbouring spike — and collapses a
few pairs onto one time.  `raw` re-centring helps units whose stderiv waveform
has several near-equal extrema, where the stderiv maximum is picked
inconsistently from spike to spike.

Reference chunk (group 6, 36–41 min, all-pairs detection, 21 units; per-unit
median of ‖w − template‖²/‖template‖², lower is tighter):

| Setting | raw space | stderiv space | unit 26 (stderiv) | unit 28 | unit 17 |
|---|---|---|---|---|---|
| historical | 0.187 | 0.485 | 15.2 | 1.19 | 0.49 |
| stderiv, ±5 | 0.187 | 0.485 | 9.4 | 1.97 | 0.49 |
| raw, ±5 | 0.195 | 0.531 | 1.64 | 0.62 | 0.66 |
| raw, ±8 | 0.175 | 0.489 | 1.66 | 0.48 | 0.66 |

The typical unit barely changes; the gain is concentrated in multi-extremum
units, at a small cost for some others (unit 17).  `raw` with ±8 was the best
tested setting.  Apply one mode to every spike — switching per spike (raw only
where the stderiv peak looks ambiguous) was tested and loses most of the gain,
because the raw trough and the stderiv peak sit a fixed, unit-specific lag
apart and mixing them splits a unit into two alignments.  Under `raw`, stderiv
waveforms carry their extremum a few samples before `peakSampleIndex` (the
steepest fall precedes the trough).

```yaml
- name: ndm_detectspikes
  parameters:
  - {name: recenterMode,      value: raw, status: Optional}
  - {name: recenterHalfWidth, value: 8,   status: Optional}
```

### Hybrid detection (`detectArms: hybrid`, engine `-H`)

The spatial derivative removes what neighbouring channels share.  That is the
point — distant, network-wide activity appears on every channel — but it also
attenuates a nearby cell whose footprint spans several adjacent sites.  On the
reference chunk such spikes reach 6–12σ on the raw trace yet only 3.6–5.5σ
after the all-pairs transform.  `detectArms: hybrid` adds a second detection
pass on the group-referenced raw signal (x − group mean, no temporal
difference) and merges the two:

1. Each arm thresholds at `thresholdFactor × 4σ` of **its own** signal, σ from
   the same noise window (this implies `thresholdNoise: stderiv`; an explicit
   `sdiff` is refused).
2. The union is sorted and an event within `refractoryPeriod` samples after a
   kept event is dropped — the single-pass refractory rule.
3. Merged events are re-centred like any detection.  Use
   `recenterHalfWidth` < `refractoryPeriod`/2: the historical window collapsed
   13 merged pairs onto one sample on the reference chunk (the engine warns).

The run log gives per-arm counts:

```
Hybrid detection (stderiv + raw arm, refractory 25 samples):
  Group 1: stderiv 11492, raw 27234 -> merged 28262 (19063 added by the raw arm)
```

What the extra events are (reference chunk, factor 1.33 ≈ 5.3σ per arm, scored
against the curated units' templates and refractory periods): about half match
a unit's template, but for most units they fall within the unit's refractory
period at chance rate — multi-unit and background activity, which the stderiv
pass exists to exclude.  The exception was a high-rate unit (26) whose matched
extra events showed a significant refractory dip (9 observed vs 24 expected),
i.e. genuinely missed spikes.  Treat the raw arm as a recall tool for curation,
not a drop-in replacement, and expect more clusters to triage.

```yaml
- name: ndm_detectspikes
  parameters:
  - {name: method,            value: stderiv, status: Optional}
  - {name: detectArms,        value: hybrid,  status: Optional}
  - {name: thresholdFactor,   value: 1.33,    status: Mandatory}
  - {name: recenterMode,      value: raw,     status: Optional}
  - {name: recenterHalfWidth, value: 8,       status: Optional}
```

The `.res` keeps the method name (`.res.stderiv.<group>`); which arms produced
it is recorded in the run log only.

### Post-detection clean-up (`flatRunMax`, `dedupSamples`; engine `-F`, `-U`)

Applied in the detection run after re-centring, before the shared `.res` is
written; rejected spikes leave `.res` and the (discarded) `.spk` together.

| Setting | Default | Effect |
|---|---|---|
| `flatRunMax: N` | off | reject a spike if any channel of its **raw** window holds more than N identical consecutive samples — zero-filled gaps (a back-filled DAC ring-buffer overrun) or a stuck line |
| `dedupSamples: N` | `0` | drop a spike re-centred within N samples of the previous kept spike of its group (earlier kept); `0` = identical positions only, `-1` = off |

The historical re-centring window collapses a few detections onto one sample
(7 of 12,324 on the reference chunk); `dedupSamples: 0` removes exactly those.

**Check the data before enabling `flatRunMax`.**  On the reference chunk
(group 6, 36–41 min) each channel carries short runs of exact zeros — about 15
per second per channel, 6–17 samples long, at upward zero crossings (the trace
reaches ≈ −100, reads 0 for 0.3–0.5 ms, then reappears at +100–170) — and only
one 8-sample stretch is flat on all channels at once.  `flatRunMax: 5` removes
31% of spikes there (3,795 of 12,324).  A back-filled ring-buffer gap zeroes
every channel; a 1-s injected gap and a 40-sample stuck channel were both
removed with no window touching either.

Run-time log line:

```
  Group 6: removed 3795 spike(s) with a flat run > 5 samples, 0 duplicate(s) within 0 samples
```


---

## `ndm_extractspikes` post-extraction filter (`flatRunMax`, `dedupSamples`)

The same two filters as detection, run after each extraction
(`process_spikefilter.py`, any method), set in the `ndm_extractspikes` node.
The flat-run test looks at two windows per spike and drops it if either fails:
the **raw source window** (`inputExtension`, normally the `.fil`) at the spike
time, and the extracted `.spk` in its own transform domain.  The raw test is the
one that catches a dropped-out channel: a spatial derivative mixes the dead channel
with its live neighbours, so in a `stderiv`/`sdiff` `.spk` it is not flat at all.

| Setting | Default | Effect |
|---|---|---|
| `flatRunMax: N` | off | drop spikes with more than N identical consecutive samples on any channel, in the raw window or the `.spk` |
| `dedupSamples: N` | `0` | drop a spike within N samples of the previous kept one; `-1` = off |

The `.res` is shared by every waveform variant, so a filtered extraction can
never just rewrite its own `.spk`: one keep-mask is applied to the shared `.res`
and to every per-spike file of the group still at the original count (other
`.spk` variants, `.fet`, `.clu`, `.clc`, post-group stages).  Files already at
the filtered count are left; files at any other count are reported and left
untouched; a symlinked alias is filtered once.  The original spike times are kept
as `<res>.prefilter`.  Re-running finds nothing to drop.

On the reference chunk with an injected 1-s zero gap (group 6, 12,269
detections kept with duplicates), extracting `stderiv_C5` with `flatRunMax: 5`
dropped 30 flat-run spikes and 7 duplicates: the shared `.res`, `.spk.standard`,
`.spk.stderiv_C5`, `.fet` and `.clu` all went to 12,232 rows, and both `.spk`
files were byte-identical to fresh re-extractions at the new `.res`.

Choosing N.  With one channel zeroed for 1 s and another stuck for 0.5 s
(80 spikes with at least 6 dropped samples in their window), a `stderiv_C5`
extraction flagged 0 of them in the `.spk` and all 80 in the raw window.  The
same recording also has short exact-zero runs at zero crossings (mostly 1–5
samples, a few up to ~17), so the raw test at N = 5 drops ~31% of spikes:

| `flatRunMax` | injected dropouts caught | other spikes dropped |
|---|---|---|
| 10 | 80 / 80 | 282 |
| 15 | 80 / 80 | 17 |
| 20 | 80 / 80 | 1 |

Use N ≈ 20 to remove channel dropouts without the zero-crossing runs.

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
