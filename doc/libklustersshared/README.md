# libklustersshared — Shared YAML Layer

`libklustersshared` is the shared Qt6 library used by all three neurosuite GUI applications
(ndmanager, neuroscope, klusters). It provides the canonical implementation of the neurosuite
YAML parameter-file format and exposes a set of data types and GUI widgets that are common
across applications.

---

## What it contains

### YAML I/O classes

These three classes handle all YAML parameter-file access. No YAML parsing logic lives in the
individual applications — everything is centralised here.

#### `ParameterYamlReader`

Parses a YAML parameter file once (`parseFile()`) and exposes the same accessor API as the
legacy XML readers. Applications can switch from XML to YAML by changing the reader type with
minimal code changes.

```cpp
ParameterYamlReader reader;
if (!reader.parseFile(path)) { /* handle error */ }

int nch      = reader.getNbChannels();
double fs    = reader.getSamplingRate();
double lfpFs = reader.getLfpSamplingRate();

QMap<int, QList<int>> spikeGroups;
QMap<int, QMap<QString,QString>> groupInfo;
reader.getSpikeDescription(nch, spikeGroups, groupInfo);
```

Key accessors:

| Group | Methods |
|---|---|
| Acquisition system | `getResolution()`, `getNbChannels()`, `getSamplingRate()`, `getVoltageRange()`, `getAmplification()`, `getOffset()` |
| Field potentials | `getLfpSamplingRate()` |
| File extensions | `getSampleRateByExtension()` |
| Anatomical groups | `getAnatomicalDescription()` (two overloads) |
| Spike detection | `getSpikeDescription()` (two overloads), `getChannelsByGroup()`, `getNbSamples()`, `getPeakSampleIndex()`, `getNbFeatures()` |
| Units (clusters) | `getUnits()` |
| NeuroScope display | `getScreenGain()`, `getChannelDisplayInfo()`, `getNeuroscopeVideoInfo()` |
| Programs | `getPrograms()`, `getProgramParameter()`, `getProgramsInformation()` |
| General info | `getDate()`, `getExperimenters()`, `getDescription()`, `getNotes()` |

**yaml-cpp 0.8 compatibility note:** yaml-cpp 0.8 changed `const operator[]` to produce an
*invalid* node (rather than an undefined one) when a key is absent. All `ParameterYamlReader`
methods are `const`, causing chained subscripts like
`m_root["neuroscope"]["channels"]["colors"]` to throw `YAML::InvalidNode` before the guard
could execute. This is fixed by the internal `safeGet2`/`safeGet3` helpers, which descend
through non-const temporaries one level at a time and return an empty `YAML::Node{}` if any
intermediate key is absent. All 13 previously unsafe chained subscripts are covered.

#### `ParameterYamlWriter`

Builds a YAML document from scratch via a series of `set*()` calls and writes it atomically
using a `.nstmp` temporary-file-plus-rename pattern so the original file is never left in a
partial state on crash.

```cpp
ParameterYamlWriter writer;
writer.setGeneralInformation(gi);
writer.setAcquisitionSystemInformation(acqInfo);
writer.setAnatomicalDescription(groups, attributes);
writer.setSpikeDetectionInformation(spikeGroups, information);
writer.setChannelDisplayInformation(colorList, offsets);
writer.writeTofile(path);  // atomic
```

`setSpikeDetectionInformation()` also writes `neuroscope/spikes/nSamples` and
`peakSampleIndex` automatically to keep both YAML schema locations in sync.

Optional string fields are emitted as the explicit YAML null scalar `~` rather than bare
`key:` lines, which is unambiguous across all yaml-cpp versions and avoids parse errors on
re-read.

#### `ParameterYamlModifier`

Reads an existing YAML file, patches specific nodes in-place, and writes the result
atomically. Used by neuroscope's `saveSession()` and klusters' cluster-save path.

```cpp
ParameterYamlModifier m;
m.parseFile(path);
m.setChannelDisplayInformation(channelColors, channelsGroups, channelDefaultOffsets);
m.setUnitsInformation(units);
m.writeToFile(path);
```

`setUnitsInformation()` preserves cluster data for electrode groups not present in the new
data — only the groups in the provided map are replaced.

#### Probe YAML extensions — `parameteryamlreader_probes.h` / `.cpp`

Free functions that extend `ParameterYamlReader` / `ParameterYamlWriter` to handle the
`probes:` top-level section and `probeId`/`shankIndex` metadata on anatomical group entries.
These are separate from the main classes to avoid altering their API.

> **YAML key naming inconsistency:** the C++ `ProbeEntry` struct uses the
> field name `id`, and `readProbesSection` / `writeProbesSection` read and
> write the YAML key `id`. The Python helper `process_setupgroups.py` and
> the downstream Python tools (`process_estimatedrift.py`,
> `process_localise.py`) normalise on the YAML key `probeId` and accept
> either on read. For interoperability, hand-written YAML should use
> `probeId` — `ndm_setupgroups` will rewrite `id` to `probeId` on first
> run. Both reads work, only one key is written per writer.

**Data types:**

| Type | Fields |
|---|---|
| `ProbeEntry` | `id` (int), `probeFile` (QString), `label` (QString), `channelOffset` (int), `anatomicalGroups` (QList\<int\>) |
| `ProbeGroupMeta` | `groupId` (int), `probeId` (int), `shankIndex` (int) |

**Free functions:**

| Function | Description |
|---|---|
| `readProbesSection(root, out, libraryPathOut)` | Parse the `probes:` sequence from the YAML root node into a `QList<ProbeEntry>` |
| `readAnatomyGroupMeta(root, out)` | Parse `probeId`/`shankIndex` from `anatomicalDescription.channelGroups` into a `QMap<int, ProbeGroupMeta>` keyed by 1-based group ID |
| `writeProbesSection(root, probes, libraryPath)` | Emit the `probes:` sequence into a YAML document node |
| `writeAnatomyGroupMeta(root, meta)` | Write `probeId`/`shankIndex` back into existing `channelGroups` entries (the sequence must already exist) |

Call `readProbesSection` / `writeProbesSection` from `ndmanagerdoc.cpp` after the main
`loadFromReader()` / `saveToWriter()` calls.

---

### Shared data types

| Type | Description |
|---|---|
| `ChannelColorEntry` | Three display colours (`color`, `groupColor`, `spikeGroupColor`) plus channel id |
| `GeneralInformation` | `date`, `experimenters`, `description`, `notes` |
| `FileInformation` | Extension, sampling rate, and optional channel mapping for one derived file |
| `NeuroscopeVideoInfo` | `rotation`, `flip`, `trajectory`, `backgroundImage` |
| `ProgramInformation` | Name, help text, and parameter map for one ndmanager plugin |
| `ProbeEntry` | One probe: `id`, `probeFile`, `label`, `channelOffset`, `anatomicalGroups` |
| `ProbeGroupMeta` | Probe/shank annotations on an anatomical group: `groupId`, `probeId`, `shankIndex` |

---

### Application-specific thin wrappers

| Class | Application | What it does |
|---|---|---|
| `NdManagerYamlReader` | ndmanager | Delegates all calls to `ParameterYamlReader` |
| `NdManagerYamlWriter` | ndmanager | `typedef` for `ParameterYamlWriter` |
| `NeuroscopeYamlReader` | neuroscope | Delegates to `ParameterYamlReader`; pre-loads video info at `parseFile()` |
| `KlustersYamlReader` | klusters | Delegates to `ParameterYamlReader`; maps `getClusterUserInformation()` onto `getUnits()` |

---

### GUI components

- `DockArea` — custom dockable panel container used by the main windows.
- `KlusterRubberBand` — selection rubber-band widget for scatter-plot views.
- `QExtendDialog` / `KlusterSeparator` — page-based dialog framework used by preferences and
  parameter dialogs.

---

## YAML schema reference

The schema mirrors the original XML schema. Top-level keys:

```yaml
parameters:        # version, creator
generalInfo:       # date, experimenters, description, notes
acquisitionSystem: # nBits, nChannels, samplingRate, voltageRange, amplification, offset
fieldPotentials:   # lfpSamplingRate
video:             # width, height, samplingRate (top-level; written by ndmanager)
files:             # list of {extension, samplingRate} for derived files
probes:            # list of probe entries (see below)
anatomicalDescription:
  channelGroups:   # list of {channels: [{id, skip}], probeId?, shankIndex?}
spikeDetection:
  channelGroups:   # list of {channels, nSamples, peakSampleIndex, nFeatures,
                   #         probeId?, shankIndex?, sitePositions_um?, klustakwik?}
units:             # list of {group, cluster, structure, type, isolationDistance, quality, notes}
neuroscope:        # version, miscellaneous, video (rotate/flip/…), spikes, channels
programs:          # list of {name, parameters: [{name, value, status}], help}
```

### `probes:` section

```yaml
probes:
  - id: 0                                   # integer, referenced by ndm_setupgroups and ndm_estimatedrift
    probeFile: neuronexus/Buzsaki64.probe   # path relative to probe library root
    label: "left CA1"                       # free text, displayed in NeuroScope / Klusters
    channelOffset: 0                        # first ADC channel for this probe (0-based)
    anatomicalGroups: [1, 2, 3, 4, 5, 6, 7, 8]  # written by ndm_setupgroups
  - id: 1
    probeFile: neuronexus/Buzsaki64.probe
    label: "right CA1"
    channelOffset: 64
    anatomicalGroups: [9, 10, 11, 12, 13, 14, 15, 16]
```

`probeFile` paths are relative to the probe library root. The library search order (lowest →
highest priority):

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH  (colon-separated)
SESSION_DIR/probes/
```

### `anatomicalDescription.channelGroups` — probe/shank annotations

When populated by `ndm_setupgroups`, each group entry carries additional metadata:

```yaml
anatomicalDescription:
  channelGroups:
    - channels:
        - {id: 0, skip: 0}
        - {id: 1, skip: 0}
        …
      probeId: 0       # index into probes[] — used by ndm_estimatedrift
      shankIndex: 0    # 0-based shank within the probe
```

These fields are optional: group entries without `probeId`/`shankIndex` are handled correctly
by all existing readers (the fields are simply absent).

### `spikeDetection.channelGroups` — geometry and per-group KlustaKwik overrides

Each spike-detection group entry may carry three optional extensions beyond the basic
`channels`/`nSamples`/`peakSampleIndex`/`nFeatures` fields:

```yaml
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3, 4, 5, 6, 7]
      nSamples: 52
      peakSampleIndex: 26
      nFeatures: 3
      probeId: 0                          # which probe this group belongs to
      shankIndex: 0                       # which shank within the probe (0-based)
      sitePositions_um:                   # inline electrode geometry (µm), per channel
        - [0,  0]
        - [22, 0]
        - [0,  20]
        - [22, 20]
        - [0,  40]
        - [22, 40]
        - [0,  60]
        - [22, 60]
      klustakwik:                         # per-group KlustaKwik overrides (tier 1)
        MergeThresh: 39.13                # χ²(12, 0.9999) for this 12-dim group
        MaxClusters: 20
        GlobalMergeIter: 50
```

`probeId` / `shankIndex` mirror the anatomical-group annotations and are used by
`ndm_estimatedrift` / `ndm_localise` / `ndm_applydrift` to reconstruct geometry.

`sitePositions_um` is a per-channel `[x_um, y_um]` array in the plane of the shank; when
present it takes precedence over looking up the geometry from the `.probe` file. Null entries
(`~`) mark missing or disabled sites.

The `klustakwik:` sub-block is the top tier of the three-tier parameter resolution used by
`ndm_klustakwik` (see [ndmanager-plugins](../ndmanager-plugins/README.md#ndm_klustakwik) for
details). Values here override `programs[ndm_klustakwik].parameters.*` and the script's
built-in defaults.

### `.probe` file format

Probe configuration files installed with the NeuroNexus library follow this schema:

```yaml
probeFile:
  version: '1.0'
  vendor: NeuroNexus
  model: Buzsaki64
  totalChannels: 64
  substrate:
    material: silicon
    thickness_um: 15
  shanks:
    count: 8
    spacing_um: 200
    length_mm: 5.0
  sites:
    count_per_shank: 8
    layout: octrode
    area_um2: 160
    spacing_um: null
    geometry:           # [[x_um, y_um], …] tip-to-base, optional
      - [0,    0]
      - [-11,  20]
      …
  channelMap:
    description: null
    map: null           # null → sequential assignment; list → explicit mapping
  notes: >
    Free-text notes about the probe.
```

When `channelMap.map` is null, `ndm_setupgroups` assigns channels sequentially:

```
shank i → [channelOffset + i × count_per_shank, …, channelOffset + (i+1) × count_per_shank − 1]
```

When `channelMap.map` is a flat or nested list, it is used directly with `channelOffset` added
to each entry.

A worked session example is in `templates/jg05-20120316.yaml`.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml) | UI framework and data types | Yes |
| yaml-cpp | YAML parsing and emission | Yes |

---

## Build order

`libklustersshared` must be installed before ndmanager or klusters.

```
libklustersshared
    ├── ndmanager
    └── klusters
```

See the [install guide](install/) for platform-specific build instructions.
