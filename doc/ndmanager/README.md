# ndmanager — Session Manager

ndmanager is a Qt6 GUI for managing electrophysiology recording sessions. It opens a `.xml` or `.yaml` parameter file, provides a tabbed interface for editing all acquisition and processing parameters, and dispatches the `ndmanager-plugins` preprocessing pipeline — either step by step or as a full batch.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml) | UI framework | Yes |
| libklustersshared | Shared YAML layer and widget library | Yes — build first |
| libxml2 | XML parameter file reading | Yes |
| yaml-cpp | YAML parameter file reading/writing | Yes |

`libklustersshared` is not available as a distro package and must be built from source before ndmanager. See the [install guide](install/linux.md).

---

## Launching

```bash
# Open an existing session
ndmanager session.xml
ndmanager session.yaml

# Start from one of the built-in templates (see Templates section below)
ndmanager /usr/local/share/ndmanager/Template-4-tetrodes-32552Hz.xml

# Crash-safe wrapper — forces software OpenGL to avoid MESA/Zink segfaults
# on Ubuntu 24.04 systems where Vulkan is present but incomplete
scripts/ndmanager-safe session.xml
```

The format (XML or YAML) is detected automatically from the file extension. Both formats are read by dispatching to either `XmlReader` or `NdManagerYamlReader`; the same UI is presented regardless of which format is on disk.

---

## What ndmanager manages

The `.xml` or `.yaml` parameter file is the central manifest for an entire session. ndmanager provides a GUI page for every section of that file.

**Acquisition system** — channel count, bit depth (nBits), sampling rate, voltage range, amplification factor, and ADC offset.

**Electrode groups** — which channels belong to each anatomical and spike-detection group. Each group carries its waveform window length (`nSamples`), peak sample index, and the number of PCA features per channel (`nFeatures`). Channels can be colour-coded; those colours propagate to neuroscope's trace display.

**Field potentials** — LFP sampling rate.

**Video** — frame rate, position-tracking sampling rate, and LED detection threshold. Video dimensions (width/height) are stored in the top-level `video` YAML section and are separate from the neuroscope-specific rotation and flip settings.

**Plugin parameters** — one page per installed `ndm_*` script. Each page exposes the parameters that script reads from the parameter file, with Mandatory/Optional/Dynamic status indicators. See [ndmanager-plugins](../ndmanager-plugins/README.md) for the complete parameter reference per script.

---

## Running the preprocessing pipeline

Plugins can be launched from ndmanager in two ways:

- **Individual step** — right-click a plugin page and select *Run*. Useful for reprocessing one step after a parameter change without re-running the full pipeline.
- **Full batch** — use **Actions → Run All** to execute the complete pipeline in the correct order, equivalent to calling `ndm_start` on the command line.

Plugin output streams into a docked process widget at the bottom of the window. Exit codes are reported; code `10` (output already exists, step skipped) is shown as a notice rather than an error.

---

## Built-in session templates

ndmanager ships with 16 XML templates covering common electrode configurations. These live in `src/ndmanager/src/` and are installed to the system data directory. Use them as starting points for new sessions:

| Template | Electrodes | Rate |
|---|---|---|
| `Template-1-tetrode-*` | 1 tetrode (4 ch) | 20 kHz or 32,552 Hz |
| `Template-2-tetrodes-*` | 2 tetrodes (8 ch) | 20 kHz or 32,552 Hz |
| `Template-3-tetrodes-*` | 3 tetrodes (12 ch) | 20 kHz or 32,552 Hz |
| `Template-4-tetrodes-*` | 4 tetrodes (16 ch) | 20 kHz or 32,552 Hz |
| `Template-8-tetrodes-*` | 8 tetrodes (32 ch) | 20 kHz or 32,552 Hz |
| `Template-16-tetrodes-*` | 16 tetrodes (64 ch) | 20 kHz or 32,552 Hz |
| `Template-4-octrodes-*` | 4 octrodes (32 ch) | 20 kHz or 32,552 Hz |
| `Template-8-octrodes-*` | 8 octrodes (64 ch) | 20 kHz or 32,552 Hz |

A full 96-channel silicon probe example in YAML format is in `templates/jg05-20120316.yaml` at the repository root.

---

## Parameter file format

Both XML and YAML formats are fully supported. YAML is preferred for new sessions. The `ndm_xml2yaml` script in `ndmanager-plugins` converts existing XML session files to YAML.

### YAML schema (abbreviated)

```yaml
parameters:
  version: "1.0"
  creator: "ndManager"
generalInfo:
  date: "2012-03-16"
  experimenters: "gravio"
  description: "hip\nca1"
acquisitionSystem:
  nBits: 16
  nChannels: 96
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
anatomicalDescription:
  channelGroups:
    - channels:
        - id: 0
          skip: 0
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 52
      peakSampleIndex: 26
      nFeatures: 3
programs:
  - name: ndm_hipass
    parameters:
      - name: windowHalfLength
        value: 16
        status: Mandatory
      - name: chunkSize
        value: 32000000
        status: Optional
    help: |
      High-pass filter the .dat file to produce .fil
```

For the full YAML schema including the `units` and `neuroscope` sections see [libklustersshared](../libklustersshared/README.md#yaml-schema-reference).

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
