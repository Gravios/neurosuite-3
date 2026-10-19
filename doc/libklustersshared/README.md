# libklustersshared — Shared YAML Layer

`libklustersshared` is the shared Qt6 library used by all three neurosuite GUI applications
(ndmanager, neuroscope, klusters). It provides the canonical implementation of the neurosuite
YAML parameter-file format and exposes a set of data types and GUI widgets that are common
across applications.

This page covers the YAML layer and the YAML schema. The whole library — its
CMake package, the `input::` engine, the preferences-page framework, the
`xcorr` realign engine, the utilities and the tests — is described in the
developer reference [`src/libklustersshared/docs/LIBRARY.md`](../../src/libklustersshared/docs/LIBRARY.md).

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
`root["neuroscope"]["channels"]["colors"]` to throw `YAML::InvalidNode` before the guard
could execute. This is fixed by the internal `safeGet2` helper, which descends
through non-const temporaries one level at a time and return an empty `YAML::Node{}` if any
intermediate key is absent. All 13 previously unsafe chained subscripts are covered.

#### `ParameterYamlWriter`

Builds a YAML document via a series of `set*()` calls and writes it atomically using a
`.nstmp` temporary-file-plus-rename pattern so the original file is never left in a partial
state on crash. The document starts empty (a new file) or, after `setBaseDocument(path)`, as a
copy of an existing parameter file: the setters then write only what they model over it, and
every other key, group entry and top-level block (`probeId`, `sitePositions_um`,
`klustakwik:`, `bitResolution_uV`, `sourceRecording`, ...) is kept. Groups are paired with
the existing ones like `ParameterYamlModifier` does (same channel set first, then position); a
group left out is removed. ndmanager saves this way, starting from the file it opened.
The text is written by `yamlformat::emitWithLayout` (`yamlformat.h`, also used by
`ParameterYamlModifier`), which takes comments, inline (flow) style, quoting and spacing from
the base file, so a save changes only the lines whose values changed.

```cpp
ParameterYamlWriter writer;
writer.setBaseDocument(openedPath);   // optional; false = keep the empty document
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
atomically. Used by NeuroScope's `NeuroscopeDoc::saveSession()`.

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

> **Probe id key:** a `probes:` entry's id is the YAML key `probeId`
> (canonical — the key spike and anatomical groups use to refer to a probe);
> the legacy key `id` is still read. Every reader — `readProbesSection`, the
> Python tools, KlustaKwik, `ndm_stripdat`, `ndm_extractemg` — takes
> `probeId`, then `id`, then the entry's 0-based position. Both writers
> (`writeProbesSection`, `ndm_setupgroups`) write `probeId` and drop a legacy
> `id`. The C++ `ProbeEntry` field is still called `id`.

**Data types:**

| Type | Fields |
|---|---|
| `ProbeEntry` | `id` (int), `probeFile` (QString), `label` (QString), `channelOffset` (int), `anatomicalGroups` (QList\<int\>), `spikeGroups` (QList\<int\>; empty = same as `anatomicalGroups`); in memory only, filled by ndmanager's Probes page: `layoutKnown`, `shankChannels`, `channelPositions` |
| `ProbeGroupMeta` | `groupId` (int), `probeId` (int), `shankIndex` (int) |

**Free functions:**

| Function | Description |
|---|---|
| `readProbesSection(root, out, libraryPathOut)` | Parse the `probes:` sequence from the YAML root node into a `QList<ProbeEntry>` |
| `readAnatomyGroupMeta(root, out)` | Parse `probeId`/`shankIndex` from `anatomicalDescription.channelGroups` into a `QMap<int, ProbeGroupMeta>` keyed by 1-based group ID |
| `writeProbesSection(root, probes, libraryPath)` | Emit the `probes:` sequence into a YAML document node |
| `writeAnatomyGroupMeta(root, meta)` | Write `probeId`/`shankIndex` back into existing `channelGroups` entries (the sequence must already exist) |

Callers: ndmanager reads the section through `NdManagerYamlReader::getProbesInformation()`
(in `ndmanagerdoc.cpp`'s `loadFromReader()`) and writes it through
`ParameterYamlWriter::setProbesInformation()`, which calls `writeProbesSection`; its Probes
page (`probepage.cpp`) and Klusters' drift geometry (`driftgeometry.cpp`) also call
`readProbesSection`.

---

### Shared data types

| Type | Description |
|---|---|
| `ChannelColorEntry` | Three display colors (`color`, `groupColor`, `spikeGroupColor`) plus channel id |
| `GeneralInformation` | `date`, `experimenters`, `description`, `notes` |
| `FileInformation` | Extension, sampling rate, and optional channel mapping for one derived file |
| `NeuroscopeVideoInfo` | `rotation`, `flip`, `trajectory`, `backgroundImage` |
| `ProgramInformation` | Name, help text, and parameter map for one ndmanager plugin |
| `ProbeEntry` | One probe: `id`, `probeFile`, `label`, `channelOffset`, `anatomicalGroups`, `spikeGroups` (plus in-memory layout fields) |
| `ProbeGroupMeta` | Probe/shank annotations on an anatomical group: `groupId`, `probeId`, `shankIndex` |

---

### Application-specific thin wrappers

These are not part of the library; each lives in its application's `src/` directory.

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
sourceRecording:   # header metadata of the recording the session was converted from
                   # (written by the converters, e.g. ndm_plx2dat; see its command page)
probes:            # list of probe entries (see below)
probeLibraryPath:  # extra probe library directory (see below)
anatomicalDescription:
  channelGroups:   # list of {channels: [{id, skip}], probeId?, shankIndex?}
spikeDetection:
  channelGroups:   # list of {channels, nSamples, peakSampleIndex, nFeatures,
                   #         probeId?, shankIndex?, sitePositions_um?, klustakwik?}
units:             # list of {group, cluster, structure, type, isolationDistance, quality, notes,
                   #          region, division, atlas, regionId}  (last four optional)
neuroscope:        # version, miscellaneous, video (rotate/flip/…), spikes, channels
programs:          # list of {name, category?, parameters: [{name, value, status, type?, default?, hidden?}], help}
template:          # only in a session template: name, description, createdFrom, created, sections
```

The apps keep a top-level key they do not model when they save, so a section
read only by scripts survives a save. Adding a section is a recipe in
[RECIPES.md](../dev/RECIPES.md#23-add-a-session-yaml-section).

### `probes:` section

```yaml
probes:
  - probeId: 0                              # integer, referenced by ndm_setupgroups and ndm_estimatedrift
    probeFile: neuronexus/Buzsaki64.probe   # path relative to probe library root
    label: "left CA1"                       # free text, shown by ndmanager's Probes page
    channelOffset: 0                        # first ADC channel for this probe (0-based)
    anatomicalGroups: [1, 2, 3, 4, 5, 6, 7, 8]  # written by ndm_setupgroups
  - probeId: 1
    probeFile: neuronexus/Buzsaki64.probe
    label: "right CA1"
    channelOffset: 64
    anatomicalGroups: [9, 10, 11, 12, 13, 14, 15, 16]
```

`probeFile` is an absolute path or a path relative to a probe library
directory. The directories searched, and their order, are listed under
[Probe library search](../ndmanager-plugins/commands/ndm_setupgroups.md#probe-library-search)
in the `ndm_setupgroups` reference; `probeLibraryPath` is one of them.

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

### `spikeDetection.channelGroups` — geometry and per-group KiloKlustaKwik overrides

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
      klustakwik:                         # per-group KiloKlustaKwik overrides (tier 1)
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
`ndm_klustakwik` (see [ndmanager-plugins](../ndmanager-plugins/commands/ndm_klustakwik.md) for
details). Values here override `programs[ndm_klustakwik].parameters.*` and the script's
built-in defaults.

### `.probe` file format

The probe files the `probes:` entries point at are specified in
[formats/probe.md](../ndmanager-plugins/formats/probe.md): the layout, how
`sites.geometry` is ordered, and how sites map to channels through
`channelOffset` and `channelMap.map`.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 ≥ 6.4 (Core, Gui, Widgets) | UI framework and data types | Yes |
| yaml-cpp | YAML parsing and emission | Yes |
| libneurosuite-core (`Neurosuite::core`) | on-disk formats (`neurofileio`), shared algorithms | Yes (built first in the monorepo) |

---

## Build order

Inside the monorepo the top-level `CMakeLists.txt` builds the libraries before
the applications that link them:

```
libneurosuite-core   (Neurosuite::core)
    └── libklustersshared   (Neurosuite::qt)
            ├── ndmanager
            ├── neuroscope
            └── klusters
```

See [BUILD.md](../BUILD.md) for build options, platforms and GPU backends.
