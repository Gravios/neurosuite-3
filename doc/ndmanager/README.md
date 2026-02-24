# ndmanager — Session Manager

ndmanager is a Qt6 GUI for managing electrophysiology recording sessions. It opens a `.xml` or `.yaml` parameter file, provides a tabbed interface for editing all acquisition and processing parameters, and dispatches the `ndmanager-plugins` preprocessing pipeline — either step by step or as a full batch.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| Qt6 (Core, Gui, Widgets, Xml) | UI framework | Yes |
| libklustersshared | Shared Qt6 widget library | Yes — build first |
| libxml2 | XML parameter file reading | Yes |
| yaml-cpp | YAML parameter file reading/writing | Yes |

`libklustersshared` is not available as a distro package and must be built from source before ndmanager. See the [install guide](install/linux.md).

---

## Launching

```bash
ndmanager session.xml

# Start from the provided template (96-channel silicon probe example)
ndmanager templates/template.xml

# Crash-safe wrapper — captures a backtrace on abnormal exit
scripts/ndmanager-safe session.xml
```

---

## What ndmanager manages

The `.xml` or `.yaml` parameter file is the central manifest for an entire session. ndmanager provides a GUI for every section of that file.

**Acquisition system** — channels, bit depth, sampling rate, voltage range, amplification, ADC offset.

**Electrode groups** — which channels belong to each group, waveform window length, peak sample index, and number of PCA features per channel. Channels can be colour-coded for identification in NeuroScope.

**Field potentials** — LFP sampling rate and channel selection.

**Video** — frame rate, position tracking sampling rate, and LED detection threshold.

**Plugin parameters** — one tab per installed `ndm_*` script. Each tab exposes the parameters that script reads from the parameter file. See [ndmanager-plugins](../ndmanager-plugins/README.md) for the full parameter reference.

---

## Running the preprocessing pipeline

Plugins can be launched from ndmanager in two ways:

- **Individual step** — right-click a plugin tab and select *Run*. Useful for reprocessing a single step after changing a parameter.
- **Full batch** — use **Actions → Run All** to execute the complete pipeline in order, equivalent to `ndm_start` on the command line.

Plugin output streams into a docked process widget. Exit codes are reported; code `10` (output already exists, step skipped) is shown as a notice rather than an error.

---

## Parameter file format

Both XML and YAML formats are supported and equivalent. The YAML format is preferred for new sessions. A complete 96-channel example is in `templates/jg05-20120316.yaml`.

### YAML schema (abbreviated)

```yaml
parameters:
  version: "1.0"
acquisitionSystem:
  nBits: 16
  nChannels: 96
  samplingRate: 32552
  voltageRange: 20
  amplification: 1000
  offset: 0
fieldPotentials:
  lfpSamplingRate: 1250
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
```

### XML schema (equivalent)

```xml
<parameters version="1.0">
  <acquisitionSystem>
    <nBits>16</nBits>
    <nChannels>96</nChannels>
    <samplingRate>32552</samplingRate>
    <voltageRange>20</voltageRange>
    <amplification>1000</amplification>
    <offset>0</offset>
  </acquisitionSystem>
  <fieldPotentials>
    <lfpSamplingRate>1250</lfpSamplingRate>
  </fieldPotentials>
  <spikeDetection>
    <channelGroups>
      <group>
        <channels>
          <channel>0</channel>
          <channel>1</channel>
        </channels>
        <nSamples>52</nSamples>
        <peakSampleIndex>26</peakSampleIndex>
        <nFeatures>3</nFeatures>
      </group>
    </channelGroups>
  </spikeDetection>
  <programs>
    <program>
      <name>ndm_hipass</name>
      <parameters>
        <parameter><name>windowHalfLength</name><value>16</value></parameter>
      </parameters>
    </program>
  </programs>
</parameters>
```

---

## Installation

| Platform | Guide |
|---|---|
| Linux (Ubuntu / Debian) | [install/linux.md](install/linux.md) |
| Windows | [install/windows.md](install/windows.md) |
| macOS | [install/macos.md](install/macos.md) |
