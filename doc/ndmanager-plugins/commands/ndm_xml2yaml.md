# `ndm_xml2yaml` — convert a legacy parameter file (`.xml` → `.yaml`)

## Synopsis

```bash
ndm_xml2yaml SESSION.xml [OUTPUT.yaml]
```

**Input:** an ndManager / NeuroScope XML parameter file

**Output:** `OUTPUT.yaml`, or `SESSION.yaml` next to the input (an `.xml` /
`.XML` extension is replaced); an existing file is overwritten without asking

## Description

A one-off migration tool (Python), not a pipeline step: it reads no
`programs:` parameters and does not use the `ndm_*` exit-code conventions.
It exits 0 on success or with `-h`/no argument (prints usage), and 1 if the
input does not exist. The XML file is not modified.

Sections converted:

| XML | YAML |
|---|---|
| root `version`, `creator` | `parameters: {version, creator}` |
| `generalInfo` | `generalInfo` (`date`, `experimenters`, `description`, `notes`) |
| `acquisitionSystem` | `acquisitionSystem` (`nBits`, `nChannels`, `samplingRate`, `voltageRange`, `amplification`, `offset`) |
| `fieldPotentials` | `fieldPotentials.lfpSamplingRate` |
| `files` | `files` (`samplingRate`, `extension`) |
| `anatomicalDescription` | `channelGroups` with `channels: [{id, skip}]` |
| `spikeDetection` | `channelGroups` with `channels`, `nSamples`, `peakSampleIndex`, `nFeatures` (missing values: 52, 26, 3) |
| `units` | `units` (dash-only placeholders such as `--` become null); `units: ~` when absent |
| `neuroscope` | `neuroscope`: `miscellaneous`, `video`, `spikes`, `channels.colors`, `channels.offsets` |
| `programs` | `programs` (`name`, `parameters`, `help`); `true`/`false` and numbers become YAML booleans and numbers |

Not converted: anything outside these sections, and channel groups with no
channels, which are dropped — the groups after them are renumbered, so check
the group count of a file that had empty groups. The XML format has no
`probes:` section, so the output has no probe geometry.

### After conversion

To give the session probe geometry (needed by `ndm_estimatedrift`,
`ndm_driftcorrect` and `ndm_localise`), add a `probes:` section and run
[`ndm_setupgroups`](ndm_setupgroups.md) with `overwrite: true` — the converted
file already has groups, and `ndm_setupgroups` refuses to replace them
otherwise. This rebuilds both group lists from the probe library.

Use the YAML from then on. The shared readers in
[`ndm_functions`](ndm_functions.md) prefer `SESSION.yaml` over
`SESSION.xml` when both exist; the XML path is kept only for old sessions
and does not carry newer fields (`probes:`, per-group `klustakwik:` or
`sdiffPairs`, method tokens).

## Parameters

None. The template's `ndm_xml2yaml` block has an empty parameter list.

## See also

- [`ndm_setupgroups`](ndm_setupgroups.md)
- [`.probe` format](../formats/probe.md)
- [ndmanager](../../ndmanager/README.md) — opens XML and saves YAML

---

*Part of the [ndmanager-plugins](../README.md) reference.*
