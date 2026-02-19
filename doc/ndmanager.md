# ndmanager — Session Manager

ndmanager is a Qt6 GUI application for managing electrophysiology recording sessions. It opens a `.xml` parameter file, provides a tabbed interface for editing all acquisition and processing parameters, and dispatches the `ndmanager-plugins` preprocessing pipeline — either step by step or as a full batch.

---

## Launching

```bash
ndmanager session.xml

# Start from the provided template (96-channel silicon probe example)
ndmanager templates/template.xml

# A crash-safe wrapper captures a backtrace on abnormal exit
scripts/ndmanager-safe session.xml
```

---

## What ndmanager manages

The `.xml` parameter file is the central manifest for an entire session. ndmanager provides a GUI for every section of that file:

**Acquisition system** — number of channels, bit depth, sampling rate, voltage range, amplification, and ADC offset.

**Electrode groups** — which physical channels belong to each electrode group, waveform window length, peak sample index, and number of PCA features per channel. Channels can be colour-coded for easy identification in NeuroScope.

**Field potentials** — LFP sampling rate and channel selection.

**Video** — frame rate, sampling rate of the position tracking system, and the LED detection threshold.

**Plugin parameters** — one tab per installed `ndm_*` script. Each tab exposes the parameters that script reads from the XML (e.g. `windowHalfLength` for `ndm_hipass`, `thresholdFactor` for `ndm_extractspikes`). See [doc/ndmanager-plugins.md](ndmanager-plugins.md) for the full parameter reference for each script.

---

## Running the preprocessing pipeline

Plugins can be launched from ndmanager in two ways:

- **Individual step** — right-click a plugin tab and select *Run*. Useful for reprocessing a single step after changing a parameter.
- **Full batch** — use **Actions → Run All** to execute the complete pipeline in order, equivalent to running `ndm_start` on the command line.

Plugin output is streamed into a docked process widget. Exit codes are reported; code `10` (output already exists, step skipped) is shown as a notice rather than an error.

---

## The `.xml` parameter file

The XML file is both human-readable and directly editable outside ndmanager. Its top-level structure:

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
          <channel>0</channel>   <!-- 0-based -->
          <channel>1</channel>
        </channels>
        <nSamples>52</nSamples>
        <peakSampleIndex>26</peakSampleIndex>
        <nFeatures>3</nFeatures>
      </group>
      <!-- one <group> per electrode group -->
    </channelGroups>
  </spikeDetection>

  <programs>
    <program>
      <n>ndm_hipass</n>
      <parameters>
        <parameter><n>windowHalfLength</n><value>16</value></parameter>
        <parameter><n>chunkSize</n><value>68719476736</value></parameter>
      </parameters>
    </program>
    <!-- one <program> block per ndm_* script -->
  </programs>

  <units>
    <!-- Written by klusters: per-cluster metadata (structure, type, quality, notes) -->
  </units>

</parameters>
```

A complete 96-channel example is provided in `templates/template.xml`.

---

## Build

```bash
cmake -B build/ndmanager ndmanager -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager -j$(nproc)
sudo cmake --install build/ndmanager
```

Requires `libklustersshared` 2.0.0 (Qt6 build) installed first. See the top-level `README.md` for build order.

**Qt6 modules required:** `Core`, `Gui`, `Widgets`, `Xml`
