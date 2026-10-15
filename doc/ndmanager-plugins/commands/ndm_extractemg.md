# `ndm_extractemg` — per-probe EMG removal and EMG events (`.dat` → `-emgclean.dat`, `.res.emg.<probe>`)

## Synopsis

```bash
ndm_extractemg [session.yaml]
```

**Input:** `SESSION.dat`; from the session YAML `acquisitionSystem.nChannels` / `samplingRate`, `probes:` and `anatomicalDescription.channelGroups` (or `spikeDetection.channelGroups`)

**Output:** `SESSION-emgclean.dat` (+ link `SESSION.emgclean`); per probe `SESSION.emg.<probe>`, `SESSION.res.emg.<probe>`, `SESSION.spk.emg.<probe>`; `SESSION.emg.meta.yaml` — see [EMG files](../formats/emg.md)

## Description

Muscle (EMG) activity reaches every channel of a probe with nearly the
same weight. `ndm_extractemg` finds that component with FastICA, one
probe at a time, subtracts it from the recording, and writes the EMG
bursts out as events in the same layout as spikes. It is a C++ port
(`process_extractemg`) of the core of the labbox MATLAB EMG-removal
toolbox (Chen 2019), in its default configuration: deflation FastICA with
the pow3 nonlinearity on high-pass filtered data.

### Channel groups

**Per-probe mode.** Each entry of `probes:` with a `probeFile` or a
non-empty `anatomicalGroups` list is one group. Its channels are the
union of the anatomical groups it lists (`anatomicalGroups` holds 1-based
indices into `anatomicalDescription.channelGroups`), keeping only
channels with `skip: 0`. Output files are suffixed with the probe's `id`
field (`SESSION.res.emg.0`, …). A probe with no live channels is skipped
with a warning.

**Fallback.** When no such probe is declared, each
`spikeDetection.channelGroups` entry is one group (all of its channels),
and the outputs are suffixed with the 1-based group number.

Every group needs at least 2 channels, and no channel may belong to two
groups — `process_extractemg` stops with an error otherwise (in the
fallback this rules out spike groups that share channels).

### Processing

1. **Identification** (per group, groups in parallel). Reads `idDuration`
   seconds of `SESSION.dat` from `idStart`, removes each channel's mean,
   applies a 4th-order Butterworth high-pass at `hpFreq`, drops the
   filter's settling time (4 / `hpFreq` s), keeps every `downsample`-th
   sample (at most 1 000 000), and runs FastICA (deflation, pow3,
   K = max(round(0.8·nch), min(16, nch)) components, tolerance 1e-4,
   seed fixed per group so reruns reproduce). The EMG component is the
   one whose loading vector is most uniform across channels, i.e. that
   maximises |Σ A[:,k] / ‖A[:,k]‖|. If FastICA stops before finding all
   K components it warns and uses those it found.
2. **Threshold.** The unmixing row is applied to the *unfiltered*
   de-meaned subsample to get the EMG trace; threshold =
   `thresholdFactor` × 1.4826 × median(|EMG|).
3. **Removal** (streaming, `chunkSamples` samples per chunk). For every
   sample, EMG[t] = Σ<sub>c</sub> (raw[t,c] − μ<sub>c</sub>) · W<sub>c</sub>,
   and clean[t,c] = raw[t,c] − A<sub>c</sub> · EMG[t] on the group's
   channels (the channel mean stays in the signal). Channels outside every
   group are copied unchanged. Every group is computed from the original
   `.dat`, not from another group's output.
4. **Events.** |EMG| is smoothed with a boxcar of `envelopeWindowMs`.
   Scanning forward, the first sample at or above the threshold and at
   least `refractoryPeriod` samples after the previous event opens a
   search window of `peakSearchLength` samples; the envelope maximum in
   it is the event time. Scanning resumes `refractoryPeriod` samples
   after that event.
5. **Waveforms.** For each event, a window of `nSamples` samples (peak at
   `peakSampleIndex`) on the group's channels is cut from the original
   `SESSION.dat` — not the filtered `.fil` and not the cleaned file.
   Windows that run past the file edge are zero-padded.

The full-length EMG trace of every group is held in memory as float32
during steps 3–4: 4 bytes × samples × groups (about 0.6 GB per probe for
2 h at 20 kHz).

The waveform shape comes from the first spike group
(`spikeDetection.channelGroups[1].nSamples` and `peakSampleIndex`),
defaulting to 32 and 16.

### Using the cleaned recording

`.dat` itself is never modified. The script links `SESSION.emgclean` →
`SESSION-emgclean.dat`, so a later step reads the cleaned recording with
`inputExtension: emgclean`:

```yaml
- name: ndm_hipass
  parameters:
    - name: inputExtension
      value: emgclean
      status: Optional
```

`ndm_hipass`, `ndm_bandpass` and `ndm_lfp` accept it (see
[signal processing](signal-processing.md) and
[parameters](../parameters.md)). Run `ndm_extractemg` before `ndm_hipass`
to detect spikes on the cleaned signal; run it after spike detection to
use the EMG events for analysis only. An existing `SESSION.emgclean` that
is a regular file, not a link, is left alone with a warning.

### Skipping and reruns

If every requested output exists — `.res.emg.<probe>` for every group,
plus `.spk.emg.<probe>`, `.emg.<probe>` and `-emgclean.dat` when enabled
— the script only (re)creates the link and exits 0. `.emg.meta.yaml` is
not checked. If any is missing, all outputs are rewritten. A missing
`SESSION.dat` or parameter file skips the step (exit 11).

### Compute backend

FastICA's inner kernel runs on the CPU (OpenMP) or on a GPU when the
binary is built with `USE_CUDA`, `USE_HIP` or `USE_SYCL` (CMake options;
only the first one found is built, in that order). `backend: auto` uses
the built GPU backend if a device is found, else the CPU; a named backend
that is not built or has no device falls back to the CPU with a warning.
Problems with N·nch < 100 000 samples run on the CPU regardless.

## Parameters

| Name | Default | Meaning |
|---|---|---|
| `hpFreq` | 100 | High-pass cutoff (Hz) of the identification data. |
| `thresholdFactor` | 5.0 | Event threshold in units of 1.4826 · median(\|EMG\|). |
| `envelopeWindowMs` | 20 | Boxcar width (ms) for smoothing \|EMG\| before event detection. |
| `refractoryPeriod` | 0 | Minimum samples between events; 0 = `envelopeWindowMs` in samples. |
| `peakSearchLength` | 0 | Samples after a crossing searched for the peak; 0 = `refractoryPeriod`. |
| `downsample` | 3 | Keep every n-th sample of the identification data. |
| `idStart` | 0 | Start (s) of the identification window. |
| `idDuration` | 60 | Length (s) of the identification window. |
| `chunkSamples` | 1048576 | Samples per channel per streaming chunk (minimum 4096). |
| `backend` | `auto` | `auto`, `cpu`, `cuda`, `hip` or `sycl`. |
| `emgClean` | `yes` | Write `SESSION-emgclean.dat` (and the link). |
| `emgTrace` | `yes` | Write `SESSION.emg.<probe>`. |
| `emgWaveforms` | `yes` | Write `SESSION.spk.emg.<probe>`. |

All are optional. For the three output switches only the value `no`
turns the output off.

```yaml
- name: ndm_extractemg
  parameters:
    - name: hpFreq
      value: 100
      status: Optional
    - name: thresholdFactor
      value: 5.0
      status: Optional
    - name: envelopeWindowMs
      value: 20
      status: Optional
    - name: refractoryPeriod
      value: 0
      status: Optional
    - name: peakSearchLength
      value: 0
      status: Optional
    - name: downsample
      value: 3
      status: Optional
    - name: idStart
      value: 0
      status: Optional
    - name: idDuration
      value: 60
      status: Optional
    - name: chunkSamples
      value: 1048576
      status: Optional
    - name: backend
      value: auto
      status: Optional
    - name: emgClean
      value: 'yes'
      status: Optional
    - name: emgTrace
      value: 'yes'
      status: Optional
    - name: emgWaveforms
      value: 'yes'
      status: Optional
```

Set `emgClean: no` while tuning the event parameters to avoid rewriting
the full-size cleaned file on every run; set `emgWaveforms: no` when
only the event times are needed.

## See also

- [EMG files](../formats/emg.md) — layouts of all outputs
- [Signal processing](signal-processing.md) — `inputExtension: emgclean`
- [`.res`](../formats/res.md) · [`.spk`](../formats/spk.md) · [naming](../formats/naming.md)
- [`ndm_setupgroups`](ndm_setupgroups.md) — probes and anatomical groups

---

*Part of the [ndmanager-plugins](../README.md) reference.*
