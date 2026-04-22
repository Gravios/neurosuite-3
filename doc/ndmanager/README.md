# ndmanager — Session Manager

ndmanager is a Qt6 GUI for managing electrophysiology recording sessions. It opens a `.yaml`
(preferred) or legacy `.xml` parameter file, provides a tabbed interface for editing all
acquisition and processing parameters, and dispatches the `ndmanager-plugins` preprocessing
pipeline — either step by step or as a full batch.

> **XML is accepted on open for backward compatibility, but the preprocessing pipeline no
> longer reads XML directly** — legacy XML files should be converted to YAML with
> `ndm_xml2yaml` before running the pipeline. ndmanager always saves YAML.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml) | UI framework | Yes |
| libklustersshared | Shared YAML layer and widget library | Yes — build first |
| libxml2 | XML parameter file reading | Yes |
| yaml-cpp | YAML parameter file reading/writing | Yes |

`libklustersshared` is not available as a distro package and must be built from source before
ndmanager. See the [install guide](install/linux.md).

---

## Launching

```bash
# Open an existing session
ndmanager session.xml
ndmanager session.yaml

# Start from the built-in YAML template
ndmanager /usr/local/share/ndmanager/templates/template.yaml

# Crash-safe wrapper — forces software OpenGL to avoid MESA/Zink segfaults
# on Ubuntu 24.04 systems where Vulkan is present but incomplete
scripts/ndmanager-safe session.yaml
```

The format (XML or YAML) is detected automatically from the file extension. Both formats are
read by dispatching to either `XmlReader` or `NdManagerYamlReader`; the same UI is presented
regardless of which format is on disk.

---

## What ndmanager manages

The `.yaml` parameter file is the central manifest for an entire session. ndmanager provides a
GUI page for every section of that file.

**Acquisition system** — channel count, bit depth (nBits), sampling rate, voltage range,
amplification factor, and ADC offset.

**Electrode groups** — which channels belong to each anatomical and spike-detection group.
Each group carries its waveform window length (`nSamples`), peak sample index, and the number
of PCA features per channel (`nFeatures`). Channels can be colour-coded; those colours
propagate to neuroscope's trace display.

**Probes** — which physical probes are connected, what `.probe` configuration file each uses,
its first ADC channel (`channelOffset`), and a user label. The Probes tab is the GUI entry
point for the `probes:` YAML section; the groups in the Electrode Groups tab are then populated
automatically by running `ndm_setupgroups`.

**Field potentials** — LFP sampling rate.

**Video** — frame rate, position-tracking sampling rate, and LED detection threshold. Video
dimensions (width/height) are stored in the top-level `video` YAML section and are separate
from the neuroscope-specific rotation and flip settings.

**Plugin parameters** — one page per installed `ndm_*` script. Each page exposes the
parameters that script reads from the parameter file, with Mandatory/Optional/Dynamic status
indicators. Parameters set here land in the `programs[name=...].parameters` section of the
YAML (tier-2 of the three-tier resolution: per-group override → global session override → bash
default). For KlustaKwik specifically, each `spikeDetection.channelGroups[g]` entry may also
carry an embedded `klustakwik:` sub-block with per-group calibrated values (tier-1, highest
priority) — these are typically written by `ndm_aom2dat` from probe topology. See
[ndmanager-plugins](../ndmanager-plugins/README.md) for the complete parameter reference per
script.

---

## The Probes tab

The Probes tab exposes the `probes:` section of the session YAML and is the starting point for
any silicon probe session.

### Fields

| Field | YAML key | Description |
|---|---|---|
| Probe ID | `probes[].probeId` | Integer identifier used by `ndm_setupgroups`, `ndm_estimatedrift`, `ndm_localise`. Legacy sessions may still use `probes[].id`; both keys are accepted on read, only `probeId` is written |
| Probe file | `probes[].probeFile` | Path relative to the probe library (e.g. `neuronexus/Buzsaki64.probe`) |
| Label | `probes[].label` | Free-text label displayed in NeuroScope and Klusters |
| Channel offset | `probes[].channelOffset` | First ADC channel index for this probe (0-based) |
| Anatomical groups | `probes[].anatomicalGroups` | List of anatomical group IDs assigned to this probe. Populated automatically by `ndm_setupgroups` |
| Library path | session-level | Override for the probe library search path (optional; leave blank to use the system default) |

### Probe library

The probe library is a directory tree of `.probe` YAML files installed at
`${CMAKE_INSTALL_DATADIR}/neurosuite/probes/` (typically
`/usr/local/share/neurosuite/probes/`). The supplied NeuroNexus library covers 50 probe models.

The **Library path** field overrides or appends to the default search path. The search order
from lowest to highest priority is:

```
/usr/share/neurosuite/probes/
/usr/local/share/neurosuite/probes/
~/.local/share/neurosuite/probes/
$NEUROSUITE_PROBE_PATH  (colon-separated environment variable)
SESSION_DIR/probes/     (session-local probes placed next to the .yaml file)
Library path field      (set in the Probes tab)
```

### Workflow

1. Open or create a session YAML.
2. On the **Probes** tab: add one row per physical probe, set `probeFile` and `channelOffset`.
3. Save the session.
4. Run `ndm_setupgroups` (from the Plugins tab or command line). This populates the Electrode
   Groups tab automatically, writing `probeId`, `shankIndex`, and `sitePositions_um` (inline
   electrode geometry) into each anatomical and spike-detection group so downstream tools
   (`ndm_estimatedrift`, `ndm_localise`, klustakwik drift-aware chunking) can reconstruct
   geometry without re-reading the probe file.
5. Verify channel assignments on the Electrode Groups tab, then proceed with the pipeline.

---

## Running the preprocessing pipeline

Plugins can be launched from ndmanager in two ways:

- **Individual step** — right-click a plugin page and select *Run*. Useful for reprocessing
  one step after a parameter change.
- **Full batch** — use **Actions → Run All** to execute the complete pipeline in the correct
  order, equivalent to calling `ndm_start` on the command line.

Plugin output streams into a docked process widget at the bottom of the window. Exit codes are
reported; code `10` (output already exists, step skipped) is shown as a notice rather than an
error.

---

## Built-in session templates

The session template at `templates/template.yaml` (installed to the system data directory)
covers a full 28-plugin pipeline including the three new analysis plugins. Use it as a starting
point for new sessions. The legacy XML templates in `src/ndmanager/src/` cover common tetrode
and octrode configurations.

---

## Parameter file format

Both XML and YAML formats are fully supported. YAML is preferred for new sessions. The
`ndm_xml2yaml` script converts existing XML session files.

### YAML schema (abbreviated)

```yaml
parameters:
  version: "1.0"
  creator: "ndManager"
generalInfo:
  date: "2024-01-15"
  experimenters: "gravio"
  description: "CA1 silicon probe"
acquisitionSystem:
  nBits: 16
  nChannels: 64
  samplingRate: 32552
  voltageRange: 20
  amplification: 1000
  offset: 0
fieldPotentials:
  lfpSamplingRate: 1250
video:
  samplingRate: 25
  width: 640
  height: 480
files:
  - extension: lfp
    samplingRate: 1250
probes:
  - probeId: 0
    probeFile: neuronexus/Buzsaki64.probe
    label: "left CA1"
    channelOffset: 0
    anatomicalGroups: [1, 2, 3, 4, 5, 6, 7, 8]
anatomicalDescription:
  channelGroups:
    - channels:
        - {id: 0, skip: 0}
        - {id: 1, skip: 0}
      probeId: 0
      shankIndex: 0
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3, 4, 5, 6, 7]
      nSamples: 52
      peakSampleIndex: 26
      nFeatures: 3
      probeId: 0
      shankIndex: 0
      sitePositions_um: [[0, 0], [22, 0], [0, 20], [22, 20],
                          [0, 40], [22, 40], [0, 60], [22, 60]]
      klustakwik:
        MergeThresh: 39.13      # χ²(12, 0.9999) for a tetrode
        MaxClusters: 20
        GlobalMergeIter: 50
programs:
  - name: ndm_setupgroups
    parameters:
      - name: nSamples
        value: 52
        status: Optional
```

For the full YAML schema see [libklustersshared](../libklustersshared/README.md#yaml-schema-reference).

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
