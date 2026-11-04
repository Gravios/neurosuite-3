# `ndm_estimategeometry` — wire-bundle site layout from channel coherence (`.dat`/`.fil` → the group's `.probe`)

## Synopsis

```bash
ndm_estimategeometry [session.yaml] [electrodeGroup] [--force] [--inputExtension EXT]
                     [--wireDiameterUm D] [--layout bundle|free] [--estimateKnown]
```

Without `session.yaml`, the session is the current folder's name
(`rat01-20261015/` → `rat01-20261015.yaml`), as for every step. A group number
can be given alone (`ndm_estimategeometry 3`).

| Option | Same as |
|---|---|
| `--force` | `overwrite: 1`: redo groups already estimated |
| `--inputExtension EXT` | `inputExtension` |
| `--wireDiameterUm D` | `wireDiameterUm` |
| `--layout bundle\|free` | `layout` |
| `--estimateKnown` | `estimateKnown: 1` |

An option on the command line wins over the YAML value, which wins over the
default (as in [`ndm_pca`](pca.md) and [`ndm_detectspikes`](spike-detection.md)).
`--option=value` works too. An unknown option, a group that is not a number or
more than two arguments stop the step with the usage, before anything runs.

**Input:** `session.dat` (raw) and/or `session.fil` (filtered); the spike
groups of the session YAML. A probe file is **optional**.

**Output:** the group's [`.probe`](../formats/probe.md) file, updated in place
(or a new one when the group has none). No other file; the session YAML is not
changed.

## Description

Estimates where the wires of a tetrode, octrode or other bundle sit in the
bundle's cross-section when the recording came without a geometry. Wires that
are close together record more of the same activity. The step measures that
for every pair of a group's channels, in several independent ways, and places
the wires so that their distances follow it.

### Which groups

- **Estimated by default:** every spike group (with at least three live
  channels) that has no geometry, or only a wire-bundle **template**: a probe
  file with `substrate.material: wire-bundle` not written by this step (the
  library's `general/tetrode.probe`, for example), whose layout was drawn, not
  measured.
- **Named groups:** a group given by `groups` or as `electrodeGroup` on the
  command line is estimated whatever geometry it has, and is not used for
  calibration. This is the way past a hand-drawn placeholder (e.g. a Probe
  Maker file marked `silicon`), which the step cannot tell from a measured
  layout. Only the named groups are estimated.
- **Already estimated** (the probe file has `probeFile.estimate` for the
  group's shank, or an unattached `session.probe.<id>.probe` was written for
  its channels): skipped, named or not, unless `--force` / `overwrite: 1`.
- **Calibration:** a group with a measured geometry — a probe file through
  `probeId`/`shankIndex`, or `sitePositions_um`, that is not a template — is
  not estimated. In a session that mixes silicon probes and wire bundles, those
  groups decide how much each similarity measure counts (`calibrate: 1`; see
  Weights); never for their own estimate. `estimateKnown: 1` estimates them
  too, as a check against their layout: the log gives the agreement, nothing
  is written. A placeholder that is not a template would calibrate too: leave
  it out with `calibrationGroups`.
- **Flat channels** (median |x| = 0) are left out of their group, listed in
  `estimate`, and their sites keep their old positions.

For every estimated group that had a layout, the log and `estimate` give the
agreement with it (Spearman and scaled Procrustes RMS). It checks the estimate
only if that layout was measured.

### Pair similarities

On `nSegments` stretches of `segmentSec` seconds spread over the recording:

| Measure | What |
|---|---|
| `dat:<lo>-<hi>` | magnitude-squared coherence of the raw `.dat` in a band (`datBands`), averaged over the band. From 300 Hz up only spike-free windows are used. |
| `fil:<lo>-<hi>` | the same on the `.fil` (`filBands`), spike-free windows: the high-frequency background of distant, unresolved spikes |
| `spike:amp` | correlation, across the detected spikes, of the two channels' log peak-to-peak amplitudes after each spike's mean is removed (its spatial profile) |
| `spike:shape` | mean correlation of the two channels' waveforms within a spike |

Spikes are detected on the `.fil` (or on the `.dat` high-passed at
`highPassHz` when there is no `.fil`) at `detectThreshold` robust SDs, on the
most negative channel. A sample is background when it is more than
`spikeGuardMs` from every detected spike. Coherences and correlations are
Fisher-transformed.

### Weights

A measure counts only as far as it follows distance:

- **With calibration groups:** its Spearman correlation with the known site
  distances, averaged over those groups (the estimated group itself left
  out), squared (≤ 0 → weight 0).
- **Without:** its split-half reliability: the Spearman correlation of the pair
  ordering between two random halves of the segments, averaged over 20 splits.
  This shows that a measure is reproducible, not that it is spatial, so an
  estimate without calibration groups rests on the assumption that the
  reproducible structure is spatial.

The combined dissimilarity of a pair is the weighted mean of its rank under
each measure.

### Layout

- `bundle` (default): the wires are placed in the cross-section by
  non-metric (ordinal) stress on the combined dissimilarity, with a hard core
  at `wireDiameterUm` (no two wires closer) and a weak pull to the centre (the
  bundle is packed). The scale comes from the packing, not from the data. 30
  starts (classical MDS and random); the best is kept.
- `free`: the same without the packing, scaled so the median
  nearest-neighbour distance is `wireDiameterUm`. Meant for checks on a
  silicon shank, whose sites are not packed.

The data fix the layout only up to a rotation and a reflection. The result is
centred, its long axis along x, its first channel at x ≥ 0.

### Uncertainty

The segments are resampled with replacement `nBootstrap` times. Each replicate
is embedded, started from the estimate, and aligned to it (rotation and
reflection). `estimate` gives each site's RMS deviation (µm) and, per wire,
its most frequent nearest neighbour and how often. The bootstrap measures
how stable the estimate is under resampling. It does not show whether the
measures are spatial: a session without spatial structure can give an equally
stable, wrong layout (see the negative control in the test). Calibration
groups and `estimateKnown` are the check for that.

### Low-coherence wires

A wire whose coherence with its best partner in the lowest `dat` band is an
outlier (robust z < −3.5) *and* at least 0.1 below the others' is listed under
`lowCoherenceWires`: a broken or high-impedance wire, or, on a silicon shank, a
site in another layer.

## Output

The estimate goes into the probe file bound to the group (`probeId` /
`shankIndex` → `probes:` entry), at the group's sites, found by channel number
as every tool binds them (`scripts/probe_geometry.py`):

- `sites.geometry`: the estimated positions, centred where the group's sites
  were (so the shanks of a multi-bundle file keep their places). The other
  sites are not touched.
- `sites.geometry_backup`: the layout the file had before the first estimate.
  Written once; a later run never overwrites it.
- For a bundle (`layout: bundle`): `substrate.material: wire-bundle` and
  `sites.plane: cross-section`. `x` and `y` are then the two axes of the
  bundle's cross-section, **not** the lateral offset and height above the tip
  of a silicon shank. Tools that read `y` as depth (`ndm_estimatedrift`) see
  the cross-section instead; a bundle has no depth axis for them to use.
- `estimate.<shank>`: the run — date, channels, flat channels left out, layout,
  wire diameter, segments and spikes, where the weights came from and the
  weights, ordinal stress, per-site SD, each wire's most frequent nearest
  neighbour, low-coherence wires, and the agreement with the previous layout.

The file's leading comment lines are kept; the rest is rewritten (key order
kept, comments inside dropped).

- **No probe file** (no `probeId`): a new `session.probe.<id>.probe` (the next
  free id, `vendor: estimated`, `channelMap` relative to the group's lowest
  channel). Add it in ndmanager's Probes tab with the `probeId` and
  `channelOffset` given in the log, and set the group's `probeId`/`shankIndex`.
  Until then a rerun finds it and treats the group as already estimated.
- **A probe file outside the session folder** (the shared library) is not
  changed: copy it into the session (ndmanager's Probes tab does) and run again.
- **`sitePositions_um`** in the session YAML (written by `ndm_setupgroups`) is
  read before the probe file and is not updated: the log says so; rerun
  `ndm_setupgroups` or remove it.

## Parameters

Set in the `ndm_estimategeometry` block (see [parameters](../parameters.md)):

| Name | Default | Meaning |
|---|---|---|
| `wireDiameterUm` | `15` | outer diameter of a wire with its insulation, µm: the smallest centre-to-centre distance |
| `layout` | `bundle` | `bundle` (packed) or `free` (ordering only) |
| `groups` | without a geometry | groups to estimate, e.g. `3` or `1,4`; named groups are estimated whatever geometry they have |
| `calibrate` | `1` | weight the measures on the groups with a geometry |
| `calibrationGroups` | all known | restrict the calibration to these groups |
| `estimateKnown` | `0` | also estimate groups with a geometry, as a check (nothing written) |
| `nSegments`, `segmentSec` | `120`, `2` | stretches used, and their length |
| `datBands` | `1-12,12-40,40-100,100-300,300-1000` | bands of the `.dat` coherence, Hz |
| `filBands` | `1000-3000,3000-6000` | bands of the `.fil` coherence, Hz |
| `detectThreshold` | `5` | spike detection, robust SDs |
| `spikeWindowMs` | `0.5` | half window for spike amplitudes |
| `spikeGuardMs` | `1` | distance from a spike for background samples |
| `highPassHz` | `800` | high-pass when there is no `.fil` |
| `inputExtension`, `filExtension` | `dat`, `fil` | the raw and the filtered file |
| `nBootstrap` | `50` | resamples for the uncertainty |
| `seed` | `0` | random seed |
| `overwrite` | `0` | redo groups already estimated |
| `probeLibrary` | — | extra probe directory for the calibration groups' probe files |

The helper is `process_estimategeometry.py` (Python 3 with `numpy`, `scipy`,
`pyyaml`).

## Validation

- **Synthetic** (`test/test_estimategeometry.py`): a packed octrode of 15 µm
  wires beside a calibration shank; 40 s at 20 kHz. Recovered within 3 µm RMS
  with 12 of 14 touching pairs. Negative control: the same sources with gain
  profiles unrelated to position give 16 µm.
- **Real, silicon** (Buzsaki64L shank, 8 sites at 20 µm pitch, 11 min,
  estimated as if unknown, without calibration): Spearman 0.97 between the
  combined dissimilarity and the true distances. Per measure 0.69–0.96; the
  `.fil` 1–3 kHz background, `dat:300-1000` and the spike shapes were the best
  (0.93–0.96), the LFP band 1–12 Hz and the spike amplitudes the weakest
  (0.69–0.70).
- **Not validated:** a real wire bundle against a measured geometry. Its
  distances (15–45 µm) are shorter than the silicon check's (40–140 µm).

## See also

- [`ndm_setupgroups`](ndm_setupgroups.md) — groups and site positions from a
  probe file
- [`.probe` format](../formats/probe.md)
- [`ndm_localise`](ndm_localise.md) — uses the site positions

---

*Part of the [ndmanager-plugins](../README.md) reference.*
