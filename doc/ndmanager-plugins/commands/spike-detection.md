# Spike detection — `ndm_detectspikes`, `ndm_extractspikes` and waveform clean-up (`.fil` → `.res`, `.spk`)

Spike processing runs in two steps.  `ndm_detectspikes` finds the spikes and
writes their times to the shared `.res`; `ndm_extractspikes` then cuts the
waveforms at those times and writes one `.spk` per method.  Both are thin
wrappers around `ndm_process_spikes` in [`ndm_functions`](ndm_functions.md) and
are configured independently, each from its own program node.

**Contents:** [`ndm_detectspikes`](#ndm_detectspikes--threshold-detection-fil--res) ·
[`ndm_extractspikes`](#ndm_extractspikes--waveform-extraction-res--spk) ·
[Methods](#methods-method-methodorder) ·
[Custom difference patterns and alignment](#custom-difference-patterns-and-alignment) ·
[`ndm_extractspikes_sdiff`, `ndm_extractspikes_stderiv`](#ndm_extractspikes_sdiff-ndm_extractspikes_stderiv--deprecated-aliases) ·
[`ndm_spikecleaner`](#ndm_spikecleaner--drop-flat--railed-waveforms) ·
[`ndm_denoiseuniform`](#ndm_denoiseuniform--remove-uniform-noise-events)

A typical sequence after [`ndm_hipass`](signal-processing.md):

```bash
ndm_detectspikes  session.yaml                      # -> session.res.stderiv.N
ndm_extractspikes session.yaml                      # -> session.spk.<method>.N
ndm_extractspikes session.yaml --method standard    # a second waveform variant, same .res
```

---

## `ndm_detectspikes` — threshold detection (`.fil` → `.res`)

### Synopsis

```bash
ndm_detectspikes [session.yaml] [--method M] [--methodOrder N] [--inputExtension EXT] [--force]
```

**Input:** `session.fil` (or `session.<inputExtension>`); acquisition settings
and `spikeDetection` groups (`channels`, `nSamples`, `peakSampleIndex`,
optional `sdiffPairs`) from the session YAML

**Output:** `session.res.<method>.N` per spike group (see [`.res` format](../formats/res.md))

### Description

Runs the engine selected by `method` in detection mode over the input file
and writes the spike times of each group to `session.res.<method>.N`.  The
engines have no detect-only mode: they also write a `.spk`, which is
discarded, so this step contributes only the `.res`.

The threshold is `thresholdFactor × 4σ` per channel, with
σ = median(|x|)/0.6745 measured over the noise window `start`…`start+duration`
(seconds).  What x is depends on the method — see [Methods](#methods-method-methodorder).
For `standard`, `process_medianthreshold` computes the thresholds and the
engine receives them; `sdiff` and `stderiv` compute them internally.

The `--method`, `--methodOrder` and `--inputExtension` flags override the YAML
values.  A group is processed only if `session.res.<method>.N` for **this**
method does not exist yet; `--force` re-detects and overwrites.  When every
group already has its `.res`, the step prints "nothing to do" and exits 0.
A group with no channels (`channels: []`) is skipped with a message; the other
groups keep their numbers.  It is an error only when no group has channels.

Spike times are method-independent and later steps resolve the `.res` across
methods (see [naming](../formats/naming.md#artifact-classes)).  Detect once.
The skip test looks only at the requested method, so running detection again
under a second method writes a second `.res` next to the first, and
`ndm_extractspikes` then prefers the one tagged with its own method.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `thresholdFactor` | — (mandatory) | threshold = `thresholdFactor × 4σ` |
| `refractoryPeriod` | — (mandatory) | samples skipped after each detection |
| `peakSearchLength` | — (mandatory) | peak-search window length (samples) |
| `start` | — (mandatory) | start of the noise window (s) |
| `duration` | — (mandatory) | length of the noise window (s) |
| `method` | `standard` | detection engine and `.res` tag: `standard`, `sdiff`, `stderiv`, `stderiv_S<order>`, `stderiv_C<order>` — see [Methods](#methods-method-methodorder) |
| `methodOrder` | `3` | spatial-derivative order 0–3 (`sdiff`/`stderiv`); legacy name `sdiffOrder` is still read |
| `inputExtension` | `fil` | input file extension |
| `thresholdNoise` | engine default `sdiff` | `stderiv` only — signal σ is measured on ([below](#threshold-noise-base-thresholdnoise-engine--t)) |
| `recenterMode` | engine default `stderiv` | `stderiv` only — `stderiv` or `raw` ([below](#re-centring-recentermode-recenterhalfwidth-engine--c--w)) |
| `recenterHalfWidth` | historical window | `stderiv` only — re-centring half-width in samples; `0` = none |
| `detectArms` | `stderiv` | `stderiv` only — `hybrid` adds a raw-signal pass ([below](#hybrid-detection-detectarms-hybrid-engine--h)) |
| `flatRunMax` | off | `stderiv` only — reject spikes whose raw window has a flat run longer than N ([below](#post-detection-clean-up-flatrunmax-dedupsamples-engine--f--u)) |
| `dedupSamples` | `0` | `stderiv` only — drop a spike within N samples of the previous kept one; `-1` = off |
| `memCapGB` | `2` | memory cap for the engine (GB) |

Setting any of `thresholdNoise`, `recenterMode`, `recenterHalfWidth`,
`detectArms: hybrid`, `flatRunMax` or `dedupSamples` with a method other than
`stderiv` is an error, not ignored.  Missing mandatory parameters stop the step
(exit 1).  Resolution order, the `general` block and `inputExtension` handling
are described in [parameters](../parameters.md).

```yaml
- name: ndm_detectspikes
  parameters:
  - {name: thresholdFactor,  value: 1.5,      status: Mandatory}
  - {name: refractoryPeriod, value: 25,       status: Mandatory}
  - {name: peakSearchLength, value: 50,       status: Mandatory}
  - {name: start,            value: 0,        status: Mandatory}
  - {name: duration,         value: 60,       status: Mandatory}
  - {name: method,           value: standard, status: Optional}
  - {name: methodOrder,      value: 3,        status: Optional}
  - {name: memCapGB,         status: Optional}
```

The remaining sections apply to the `stderiv` engine only.

### Threshold noise base (`thresholdNoise`, engine `-T`)

`thresholdNoise` chooses the signal σ is measured on:

| Value | σ measured on | Threshold on the detection signal |
|---|---|---|
| `sdiff` (default) | spatial derivative only, *before* the temporal difference | higher than `factor × 4σ` — the differenced signal is quieter |
| `stderiv` | the spatial + temporal derivative actually thresholded | exactly `factor × 4σ` |

`sdiff` stays the default so existing sessions reproduce.  On the reference
session (group 6, 36–41 min) the temporal difference has ~0.6× the noise of the
spatial derivative, so `thresholdFactor` 0.8 lands at 5.1–5.5σ of the detection
signal rather than 3.2σ.  With `stderiv`, re-pick the factor: ~1.33 reproduces
the `sdiff` level at 0.8 (11.5k vs 12.3k spikes in that 5-minute window), 1.15
gives ~4.6σ (21.7k), 1.0 gives 4σ (40.6k).  Every run prints the effective
threshold in detection-signal σ, e.g.

```
Group 6 noise base: sdiff (historical); effective threshold 5.13..5.49 sigma of the detection signal (factor 0.8 x 4 = 3.2)
```

```yaml
- name: ndm_detectspikes
  parameters:
  - {name: method,          value: stderiv, status: Optional}
  - {name: thresholdNoise,  value: stderiv, status: Optional}
  - {name: thresholdFactor, value: 1.33,    status: Mandatory}
```

### Re-centring (`recenterMode`, `recenterHalfWidth`; engine `-C`, `-W`)

After detection each spike is moved to a refined position, and that position
is what the shared `.res` records.  `ndm_extractspikes` extracts exactly there;
it does not re-centre.  (`.res` files written by engine builds before
2026-10-08 may hold un-re-centred detection times for some groups.)

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

Applied after re-centring, before the shared `.res` is written.

| Setting | Default | Effect |
|---|---|---|
| `flatRunMax: N` | off (unset or `0`) | reject a spike if any channel of its **raw** window holds more than N identical consecutive samples — zero-filled gaps (a back-filled DAC ring-buffer overrun) or a stuck line |
| `dedupSamples: N` | `0` | drop a spike re-centred within N samples of the previous kept spike of its group (earlier kept); `0` = identical positions only, `-1` = off |

The historical re-centring window collapses a few detections onto one sample
(7 of 12,324 on the reference chunk); `dedupSamples: 0` removes exactly those.

**Check the data before enabling `flatRunMax`.**  A `.fil` made by `ndm_hipass`
(or `ndm_bandpass` before its low-pass) is *centre sample − window median*, which is
exactly 0 whenever the centre sample is the window median: at many sign changes
(single zeros between values of ±50 or so) and along steep monotonic stretches
such as spike downstrokes (runs of up to ~17 zeros).  About 4–5% of `.fil`
samples are exact zeros for this reason; a median high-pass of clean synthetic
data reproduces the rate and run lengths, and the reference session's raw `.dat`
(group 6, 36–41 min) has no constant run longer than 4 samples and no
single-sample glitch at 8σ.  These zeros are filter output, not dropouts.
`flatRunMax: 5` removes 31% of spikes there (3,795 of 12,324).  A back-filled
ring-buffer gap zeroes every channel; a 1-s injected gap and a 40-sample stuck
channel were both removed with no window touching either.  Use N ≈ 20 (see the
[table below](#post-extraction-filter-flatrunmax-dedupsamples)).

Run-time log line:

```
  Group 6: removed 3795 spike(s) with a flat run > 5 samples, 0 duplicate(s) within 0 samples
```

---

## `ndm_extractspikes` — waveform extraction (`.res` → `.spk`)

### Synopsis

```bash
ndm_extractspikes [session.yaml] [--method M] [--methodOrder N] [--inputExtension EXT] [--force]
```

**Input:** `session.fil` (or `session.<inputExtension>`); the shared
`session.res.<m>.N` from `ndm_detectspikes`; `spikeDetection` groups from the
session YAML

**Output:** `session.spk.<method>.N` per spike group (see [`.spk` format](../formats/spk.md));
the post-extraction filter may also shrink the shared `.res` and other
per-spike files (below)

### Description

Re-extracts (engine `-R`) a waveform for every spike in the group's shared
`.res` and writes `session.spk.<method>.N` at full group width.  It never
detects.  The `.res` is found across methods: this method's tag first, then
`standard`, `stderiv`, `sdiff`, any other tag, then the untagged legacy
`session.res.N`.  With no `.res` the step stops with an error; run
`ndm_detectspikes` first.

Because spike times are method-independent, the extraction method may differ
from the detection method, and the step can be run several times with different
methods, each reusing the same `.res`.  Extraction needs no threshold or window
parameters: only the group geometry and, for `sdiff`/`stderiv`, the spatial
transform.

A group is processed only if `session.spk.<method>.N` does not exist yet;
`--force` re-extracts and overwrites.  When every group already has its `.spk`,
the step prints "nothing to do" and exits 0.  A group with no channels is
skipped, as in detection.

All groups are extracted with the `nSamples`/`peakSampleIndex` of the first
group processed; a group with different values gets a warning.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `method` | `standard` | waveform engine and `.spk` tag — see [Methods](#methods-method-methodorder) |
| `methodOrder` | `3` | spatial-derivative order 0–3 (`sdiff`/`stderiv`); legacy `sdiffOrder` is still read |
| `inputExtension` | `fil` | input file extension |
| `memCapGB` | `2` | memory cap for the engine (GB) |
| `flatRunMax` | off | post-extraction filter: drop spikes with a flat run longer than N samples (any method) |
| `dedupSamples` | `0` | post-extraction filter: drop a spike within N samples of the previous kept one; `-1` = off |

```yaml
- name: ndm_extractspikes
  parameters:
  - {name: method,      value: standard, status: Optional}
  - {name: methodOrder, value: 3,        status: Optional}
```

### Post-extraction filter (`flatRunMax`, `dedupSamples`)

The same two filters as detection, run after each extraction by
`process_spikefilter.py`, for any method.  With the defaults (`flatRunMax`
unset, `dedupSamples` 0) the filter runs on every extraction and removes only
spikes at identical positions; set `dedupSamples: -1` and leave `flatRunMax`
unset to skip it.

The flat-run test looks at two windows per spike and drops it if either fails:
the **raw source window** (`inputExtension`, normally the `.fil`) at the spike
time, and the extracted `.spk` in its own transform domain.  The raw test is the
one that catches a dropped-out channel: a spatial derivative mixes the dead channel
with its live neighbours, so in a `stderiv`/`sdiff` `.spk` it is not flat at all.

| Setting | Default | Effect |
|---|---|---|
| `flatRunMax: N` | off (unset or `0`) | drop spikes with more than N identical consecutive samples on any channel, in the raw window or the `.spk` |
| `dedupSamples: N` | `0` | drop a spike within N samples of the previous kept one (earlier kept); `-1` = off |

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
median high-pass that made the `.fil` produces short exact-zero runs (mostly 1–5
samples, a few up to ~17; see [above](#post-detection-clean-up-flatrunmax-dedupsamples-engine--f--u)),
so the raw test at N = 5 drops ~31% of spikes:

| `flatRunMax` | injected dropouts caught | other spikes dropped |
|---|---|---|
| 10 | 80 / 80 | 282 |
| 15 | 80 / 80 | 17 |
| 20 | 80 / 80 | 1 |

Use N ≈ 20 to remove channel dropouts without the median filter's zero runs.

```yaml
- name: ndm_extractspikes
  parameters:
  - {name: method,       value: stderiv_C5, status: Optional}
  - {name: flatRunMax,   value: 20,         status: Optional}
  - {name: dedupSamples, value: 0,          status: Optional}
```

---

## Methods (`method`, `methodOrder`)

`method` selects the engine and becomes the file tag.  It is read separately
by each step, so detection and extraction can use different methods.  Token
grammar and validation are described in [naming](../formats/naming.md#method-tokens).

| Family | Engine | Detection signal | Waveform written by `ndm_extractspikes` |
|---|---|---|---|
| `standard` | `process_extractspikes` | the `.fil` itself; thresholds from `process_medianthreshold` | raw `.fil` snippet |
| `sdiff` | `process_extractspikes_sdiff` | spatial derivative across the group's channels | spatial derivative of the snippet |
| `stderiv` | `process_extractspikes_stderiv` | spatial derivative, then temporal first difference: `stderiv[t] = sdiff[t] − sdiff[t−1]` | spatial + temporal derivative of the snippet |

For `stderiv` the transform is applied at extraction: `.spk.stderiv*.N` holds
the transformed waveform at full group width, and what `ndm_pca` does
downstream is a channel reduction on that file, not the transform itself (see
[PCA](pca.md)).  Common-mode noise is rejected twice (spatial, then temporal),
which is why `stderiv` is the usual choice for high-density probes.

Spatial-derivative orders (`methodOrder`, engine `-d`; default 3):

| Order | Name | Formula |
|---|---|---|
| `0` | SDIFF_NONE | `s[i] = x[i]` (for `stderiv`: temporal first difference only) |
| `1` | SDIFF_FIRST | `s[i] = x[i] − x[i+1]` (nearest neighbour) |
| `2` | SDIFF_LAPLACIAN | `s[i] = x[i] − 0.5 × (x[i-1] + x[i+1])` (discrete Laplacian) |
| `3` | SDIFF_ALLPAIRS | `s[i] = n × x[i] − Σⱼ x[j]` (default; no probe-order requirement) |

Orders 1 and 3 (and the custom orders 4 and 5) leave one linearly dependent
channel, which the PCA step drops.  Orders 1 and 2 depend on the channel order
of the group.  A one-channel group has no neighbours, so `sdiff` and `stderiv`
refuse any order but 0 on it, naming the group: use order 0, the `standard`
method, or group the channel with its neighbours.

Qualified `stderiv` tokens state the transform in the file name:

| Token | Transform |
|---|---|
| `stderiv` | `methodOrder`; if any group defines `sdiffPairs`, that pattern is applied instead (and a disagreeing `methodOrder` is reported as ignored) |
| `stderiv_S<1-3>` | plain spatial derivative of that order; `sdiffPairs` deliberately unused.  The token's order overrides `methodOrder` |
| `stderiv_C4`, `stderiv_C5` | the groups' `sdiffPairs` pattern: order 4 single-partner, order 5 reference-set.  Refused when no group defines `sdiffPairs` or when the digit disagrees with the pattern |

### Custom difference patterns and alignment

A spike group may carry an `sdiffPairs` pattern (group-local, 0-based channel
positions) that replaces the plain spatial derivative (`stderiv` engine only,
engine `-P`).  Its grammar fixes the order:

- `a-b,c-d,…` — single-partner map (order 4): output channel a = x[a] − x[b];
- `a-b+c+d,…` — per-channel reference set (order 5): output channel a =
  x[a] − mean(x[b], x[c], x[d]); every channel needs a set.

Positions are plain non-negative integers.  An order-4 pattern must be a tree:
every channel's chain of partners reaches the root, which must be the last
position (`0-1,1-2,2-3` is valid; `0-1,1-0,2-3` is not — 0 and 1 form a
cycle).  A reference set may not repeat a channel.  A malformed pattern stops
the engine with an error naming it; it is never read as a different pattern.
The order is decided once for the whole `-P` value: a single `+` in any
group makes every group order 5 (a group written `a-b` then has the
one-member set `{b}`).

A group whose pattern is empty or does not match its size falls back to
order 3.  Select the pattern with a `_C` token — `stderiv_C4` or `stderiv_C5`.

Orders 4 and 5 are **not** selectable through `methodOrder`: the engines' `-d`
accepts 0–3 only, and `-d` is never passed alongside `-P` (it is range-checked
first and would exit before `-P` is read).  `methodOrder: 4` or `5` is refused
with a pointer to the `_C` tokens.

`ndm_alignspikes` and `ndm_alignspikes_pca` re-extract from `.fil`, so they
must apply the *same* spatial operator the `.spk` was built with; for a `_C`
method they pass the group's pattern through `-P`, and skip a group that has no
pattern (with a warning) rather than aligning it with all-pairs.  A bare
`stderiv` token passes the pattern when the group has one, as extraction does.
`_S1`/`_S2` are refused — first-difference and Laplacian are not implemented in
the aligners.

Alignment rewrites `.res` so `.res[i]` marks the true peak, archiving the
original as `<res>.prealign`.  Because `.res` is method-independent, the
corrected times serve every later extraction.

---

## `ndm_extractspikes_sdiff`, `ndm_extractspikes_stderiv` — deprecated aliases

```bash
ndm_extractspikes_sdiff   [session.yaml] [flags]   # = ndm_detectspikes --method sdiff   && ndm_extractspikes --method sdiff
ndm_extractspikes_stderiv [session.yaml] [flags]   # = ndm_detectspikes --method stderiv && ndm_extractspikes --method stderiv
```

Each prints a deprecation notice and runs detection then extraction with its
method; extraction runs only if detection succeeded.  Other arguments are
passed to both steps.  Parameters are read from the `ndm_detectspikes` /
`ndm_extractspikes` nodes first and fall back to the legacy
`ndm_extractspikes_sdiff` / `ndm_extractspikes_stderiv` node, so old session
files keep working.  Because the fallback applies to both steps, `flatRunMax`
or `dedupSamples` in a legacy `ndm_extractspikes_sdiff` node is refused by
the detection step (they are `stderiv`-only there).  New sessions should list
the two steps separately.

---

## `ndm_spikecleaner` — drop flat / railed waveforms

### Synopsis

```bash
ndm_spikecleaner [session.yaml] [group]
```

**Input:** `session.res.N`, `session.spk.N` (untagged names)

**Output:** the same files rewritten in place, plus `session.fet.N` /
`session.clu.N` when present; originals in `session_spikecleaner_backup/`

### Description

Removes spike waveforms in which one or more channels are entirely flat:
**cutout** — every sample is exactly zero (ADC dropout, broken wire) — or
**flat** — zero variance after DC removal (amplifier stuck at a constant).
Run after extraction and before PCA, so degenerate waveforms do not reach the
feature space.  Without a group argument every group is processed.

Before modifying a group it copies `.res`, `.spk`, `.fet` and `.clu` to
`session_spikecleaner_backup/<file>.bak` (an existing backup is kept).

The step reads only the untagged legacy names `session.res.N` /
`session.spk.N`.  It does not resolve the method-tagged files written by
`ndm_detectspikes` / `ndm_extractspikes`, so on such a session it reports the
inputs missing and skips (exit 11).  The `flatRunMax` filter of
[`ndm_extractspikes`](#post-extraction-filter-flatrunmax-dedupsamples)
covers the same failure mode on the current file layout.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `removeFlatChannels` | `1` | also remove constant (non-zero) channels; `0` = all-zero channels only |
| `dryRun` | `0` | `1` = report counts without modifying files |

```yaml
- name: ndm_spikecleaner
  parameters:
  - {name: removeFlatChannels, value: 1, status: Optional}
  - {name: dryRun,             value: 0, status: Optional}
```

---

## `ndm_denoiseuniform` — remove uniform-noise events

### Synopsis

```bash
ndm_denoiseuniform [session.yaml] [group]
```

**Input:** `session.res.N`, `session.spk.N` (untagged names)

**Output:** the same files rewritten in place, plus `session.fet.N` /
`session.clu.N` when present; originals in `session_denoise_backup/`

### Description

Removes events that look the same on every channel of a group — common-mode
artefacts, motion noise, electrical interference.  For each waveform, after
per-channel DC removal, the **spatial uniformity score** is the mean over
samples of the across-channel standard deviation divided by the overall RMS:
≈ 0 means all channels identical (noise), ≈ 1 maximally diverse (a spike).
Waveforms scoring below `uniformityThreshold` are removed; with `removeFlat`,
waveforms with essentially no AC energy (RMS < 0.5 ADC counts) are removed too.
Spikes whose raw peak is below `minAmplitude` are always kept.  Groups with
fewer than two channels are skipped.  Run before `ndm_pca`.

Before modifying a group it copies `.res`, `.spk`, `.clu` and `.fet` to
`session_denoise_backup/<file>.bak` (an existing backup is kept).

Like `ndm_spikecleaner`, it reads only the untagged names `session.res.N` /
`session.spk.N` and skips (exit 11) on a session whose files are
method-tagged.  For `stderiv` waveforms the transform already rejects most
common-mode events, so this step is rarely needed there.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `uniformityThreshold` | `0.30` | score below which a waveform is removed; higher removes more |
| `minAmplitude` | `0` | minimum raw peak (ADC counts) for a spike to be scored; `0` = score all |
| `removeFlat` | `1` | also remove waveforms with no AC energy; `0` keeps them |
| `dryRun` | `0` | `1` = report without modifying files |

```yaml
- name: ndm_denoiseuniform
  parameters:
  - {name: uniformityThreshold, value: 0.30, status: Optional}
  - {name: minAmplitude,        value: 0,    status: Optional}
  - {name: removeFlat,          value: 1,    status: Optional}
  - {name: dryRun,              value: 0,    status: Optional}
```

---

## See also

- [`ndm_hipass`, `ndm_bandpass`](signal-processing.md) — make the `.fil`
- [`ndm_setupgroups`](ndm_setupgroups.md), [`ndm_spikegrouper`](ndm_spikegrouper.md) — define the spike groups
- [PCA feature extraction](pca.md) — the next step
- [`ndm_reextractspikes`](ndm_reextractspikes.md) — masked second-pass detection
- [File naming and method tokens](../formats/naming.md), [`.res`](../formats/res.md), [`.spk`](../formats/spk.md)
- [Parameters](../parameters.md), [first-time sort workflow](../../workflows/first-time-sort.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
