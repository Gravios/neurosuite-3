# `ndm_spikegrouper` — split spike groups by spike coincidence (`.fil` → `spikeDetection` groups in the YAML)

## Synopsis

```bash
ndm_spikegrouper [session.yaml]
```

**Input:** `session.fil`; `acquisitionSystem`, `anatomicalDescription` and
`spikeDetection` groups from the session YAML

**Output:** the session YAML, with `spikeDetection.channelGroups` rewritten

## Description

Re-partitions the spike groups from the activity in the `.fil`, so that
channels that see the same spikes stay together.  Run it after
[`ndm_hipass`](signal-processing.md) and before
[`ndm_detectspikes`](spike-detection.md), which then uses the new groups.

The helper `process_spikegrouper` (C++, CUDA when built with it, otherwise
OpenMP) works on the first `windowSec` seconds of the `.fil`:

1. **Scope.** Each `anatomicalDescription` group (a shank or probe region) is
   processed separately — coincidence across shanks is never computed.  Its
   channels are intersected with the channels currently in `spikeDetection`
   (channels left out of the spike groups stay out) minus `excludeChannels`.
2. **Detection.** Per channel, threshold = `thresholdFactor × σ` with
   σ = median(|x|)/0.6745, crossings separated by at least `refractoryMs`.
   There is no `× 4` here, unlike `ndm_detectspikes`.
3. **Similarity.** Fraction of channel-i events with a channel-j event within
   ±`coincidenceMs`, symmetrised.
4. **Clustering.** Ward clustering on 1 − similarity; k from 1 to
   `maxSubGroups` (and at most channels/`minChannels`) is chosen by the best
   silhouette score among partitions whose clusters all have at least
   `minChannels` channels.  Clusters are then turned into contiguous blocks of
   channel numbers, and a block larger than `maxMergedSize` is cut into pieces.
5. **Merging.** Adjacent blocks are merged greedily, highest mean coincidence
   first, while the result stays within `maxMergedSize` channels.
6. **Overlap.** With `channelOverlap: N`, each group also gets the last N
   channels of the previous group and the first N of the next, so neighbouring
   groups share channels.

An anatomical group with fewer than `2 × minChannels` usable channels is kept
as one group.  Each new group inherits `nSamples`, `peakSampleIndex` and
`nFeatures` from the existing spike group that holds its first channel.

The YAML is rewritten in place (via a temporary file and rename).  The whole
`spikeDetection` block is regenerated with only `channels`, `nSamples`,
`peakSampleIndex` and `nFeatures` per group.  Every other key in it is
dropped, along with comments inside the block: `sdiffPairs`, per-group
`klustakwik:` overrides, and the `probeId`, `shankIndex` and
`sitePositions_um` written by [`ndm_setupgroups`](ndm_setupgroups.md), which
drift estimation and localisation read.  `anatomicalDescription` and the rest
of the file are left untouched.  Keep a copy of the YAML if you need those keys.

The step always runs (there is no "outputs exist" skip); running it again
re-partitions the current groups.  It exits 11 when `session.fil` or the YAML
is missing, and 1 when the YAML has no `spikeDetection` or
`anatomicalDescription` groups.  It always reads `session.fil`;
`inputExtension` is not used.

## Parameters

| Name | Default | Meaning |
|---|---|---|
| `thresholdFactor` | `3.0` | per-channel threshold = factor × median(\|x\|)/0.6745 |
| `refractoryMs` | `1.0` | minimum interval between two detections on one channel (ms) |
| `coincidenceMs` | `0.4` | half-width of the coincidence window (ms) |
| `windowSec` | `60` | seconds of `.fil` analysed, from the start of the file |
| `maxSubGroups` | `16` | largest k tried per anatomical group |
| `minChannels` | `4` | minimum channels per group |
| `maxMergedSize` | `12` | maximum channels per group (before overlap); raised to `minChannels` if smaller |
| `channelOverlap` | `0` | channels borrowed from each neighbouring group |
| `excludeChannels` | empty | comma-separated channel numbers to leave out of every group, e.g. `3,7,42` |

Sampling rate, channel count and bit depth come from `acquisitionSystem`.
See [parameters](../parameters.md) for how values are resolved.

```yaml
- name: ndm_spikegrouper
  parameters:
  - {name: thresholdFactor, value: 3.0, status: Mandatory}
  - {name: refractoryMs,    value: 1.0, status: Mandatory}
  - {name: coincidenceMs,   value: 0.4, status: Mandatory}
  - {name: windowSec,       value: 60,  status: Mandatory}
  - {name: maxSubGroups,    value: 16,  status: Mandatory}
  - {name: minChannels,     value: 4,   status: Mandatory}
  - {name: maxMergedSize,   value: 12,  status: Mandatory}
  - {name: channelOverlap,  value: 0,   status: Mandatory}
```

All values have defaults in the script, so none is actually required despite
the `Mandatory` status in the template.

## See also

- [`ndm_setupgroups`](ndm_setupgroups.md) — build the initial groups from the probe library
- [Spike detection](spike-detection.md) — the next step
- [`ndm_hipass`](signal-processing.md) — makes the `.fil`

---

*Part of the [ndmanager-plugins](../README.md) reference.*
