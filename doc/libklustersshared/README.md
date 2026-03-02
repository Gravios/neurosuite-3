# libklustersshared — Shared YAML Layer

`libklustersshared` is the shared Qt6 library used by all three neurosuite GUI applications (ndmanager, neuroscope, klusters). It provides the canonical implementation of the neurosuite YAML parameter-file format and exposes a set of data types and GUI widgets that are common across applications.

---

## What it contains

### YAML I/O classes

These three classes handle all YAML parameter-file access. No YAML parsing logic lives in the individual applications — everything is centralised here.

#### `ParameterYamlReader`

Parses a YAML parameter file once (`parseFile()`) and exposes the same accessor API as the legacy XML readers. Applications can switch from XML to YAML by changing the reader type with minimal code changes.

The class provides two overloads of `getAnatomicalDescription()` and `getSpikeDescription()` to match the different caller signatures used by ndmanager versus neuroscope/klusters.

```cpp
ParameterYamlReader reader;
if (!reader.parseFile(path)) { /* handle error */ }

int nch         = reader.getNbChannels();
double fs       = reader.getSamplingRate();
double lfpFs    = reader.getLfpSamplingRate();

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

#### `ParameterYamlWriter`

Builds a YAML document from scratch via a series of `set*()` calls and writes it atomically using a `.nstmp` temporary-file-plus-rename pattern so the original file is never left in a partial state on crash.

Used by ndmanager's "Save" and "Save As" paths.

```cpp
ParameterYamlWriter writer;
writer.setGeneralInformation(gi);
writer.setAcquisitionSystemInformation(acqInfo);
writer.setAnatomicalDescription(groups, attributes);
writer.setSpikeDetectionInformation(spikeGroups, information);
writer.setChannelDisplayInformation(colorList, offsets);
// ...
writer.writeTofile(path);  // atomic
```

`setSpikeDetectionInformation()` also writes `neuroscope/spikes/nSamples` and `peakSampleIndex` automatically to keep both YAML schema locations in sync.

#### `ParameterYamlModifier`

Reads an existing YAML file, patches specific nodes in-place, and writes the result atomically. Used by neuroscope's `saveSession()` and klusters' cluster-save path, where only a subset of the file needs updating.

Supports both modify-existing and create-from-scratch workflows:

```cpp
// Modify an existing file
ParameterYamlModifier m;
m.parseFile(path);
m.setChannelDisplayInformation(channelColors, channelsGroups, channelDefaultOffsets);
m.setUnitsInformation(units);
m.writeToFile(path);

// Create a new file (parseFile not called)
ParameterYamlModifier m;
m.setAcquisitionSystemInformation(...);
m.writeToFile(newPath);
```

`setUnitsInformation()` preserves cluster data for electrode groups not present in the new data — only the groups in the provided map are replaced.

---

### Shared data types

These value types are used throughout the YAML I/O classes and are also available to application code.

| Type | Description |
|---|---|
| `ChannelColorEntry` | Three display colours (`color`, `groupColor`, `spikeGroupColor`) plus channel id. Used by ndmanager and neuroscope. `ChannelColorEntry` is typedef'd as `ChannelColors` in ndmanager and as `ChannelDescription` in neuroscope. |
| `GeneralInformation` | `date`, `experimenters`, `description`, `notes` from the `generalInfo` YAML section. |
| `FileInformation` | Extension, sampling rate, and optional channel mapping for one derived file (e.g. `.lfp`). |
| `NeuroscopeVideoInfo` | `rotation`, `flip`, `trajectory`, and `backgroundImage` from the `neuroscope/video` YAML section. |
| `ProgramInformation` | Name, help text, and parameter map for one ndmanager plugin. |

---

### Application-specific thin wrappers

Each application provides a thin delegator class so its existing code compiles with minimal changes:

| Class | Application | What it does |
|---|---|---|
| `NdManagerYamlReader` | ndmanager | Delegates all calls to `ParameterYamlReader`. |
| `NdManagerYamlWriter` | ndmanager | `typedef` for `ParameterYamlWriter`. |
| `NeuroscopeYamlReader` | neuroscope | Delegates to `ParameterYamlReader`; pre-loads video info at `parseFile()` so video getters are O(1). |
| `KlustersYamlReader` | klusters | Delegates to `ParameterYamlReader`; maps `getClusterUserInformation()` onto `getUnits()`. |

---

### GUI components

The library also contains the docking and widget infrastructure shared by ndmanager and klusters:

- `DockArea` — custom dockable panel container used by the main windows.
- `KlusterRubberBand` — selection rubber-band widget for scatter-plot views.
- `QExtendDialog` / `KlusterSeparator` — page-based dialog framework used by both applications' preferences and parameter dialogs.

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
anatomicalDescription:
  channelGroups:   # list of {channels: [{id, skip}]}
spikeDetection:
  channelGroups:   # list of {channels, nSamples, peakSampleIndex, nFeatures}
units:             # list of {group, cluster, structure, type, isolationDistance, quality, notes}
neuroscope:        # version, miscellaneous, video (rotate/flip/…), spikes, channels
programs:          # list of {name, parameters: [{name, value, status}], help}
```

A worked example is in `templates/jg05-20120316.yaml`.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml) | UI framework and data types | Yes |
| yaml-cpp | YAML parsing and emission | Yes |

---

## Build order

`libklustersshared` must be installed before ndmanager or klusters. neuroscope does not depend on it.

```
libklustersshared
    ├── ndmanager
    └── klusters
```

See the [install guide](install/) for platform-specific build instructions.
