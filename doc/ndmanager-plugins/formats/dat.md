# `.dat` / `.fil` / `.lfp` — interleaved binary signal files

The continuous signals of a session — wide-band recording, high-pass filtered
copy, LFP — and every other raw stream a step can read through
`inputExtension` share one layout: headerless, little-endian `int16`, channels
interleaved.

**Contents:** [Layout](#layout) · [The files](#the-files) ·
[Reading another file through `inputExtension`](#reading-another-file-through-inputextension) ·
[Readers](#readers)

## Layout

```
value(sample t, channel c)  at  int16 index  t × nChannels + c
byte offset                 =   (t × nChannels + c) × 2
nSamples                    =   fileSize / (nChannels × 2)
```

- **Element:** signed 16-bit integer, little-endian, raw ADC counts.
- **Order:** sample-major — all channels of sample 0, then all channels of
  sample 1, … Channel `c` is the 0-based channel index used everywhere in the
  session YAML.
- **Header:** none. Channel count and sampling rate come from the session YAML,
  not the file; a wrong channel count still reads, as the wrong numbers.
- **Time:** sample `t` of a file is at `t / samplingRate` seconds. Spike times
  in [`.res`](res.md) are sample indices into the `.fil` (acquisition rate).

| File | Channels | Sampling rate (session YAML) |
|---|---|---|
| `.dat`, `.fil`, `-spkclean.dat`, `-emgclean.dat` | `acquisitionSystem.nChannels` | `acquisitionSystem.samplingRate` |
| `.lfp` (legacy name `.eeg`) | `acquisitionSystem.nChannels` | `fieldPotentials.lfpSamplingRate` |
| `.px<stream>` | its own; see [`.px<stream>`](pxstream.md) | its own |

`acquisitionSystem.nBits` gives the sample width. The pipeline's filters, LFP
resampler and spike engines read and write `int16` only (`nBits` 16, or 12/14
stored in 16 bits). [`ndm_slicebinary`](../commands/signal-processing.md) also
handles 32-bit (`int32`) files and refuses any other width.

## The files

| File | Written by | Content |
|---|---|---|
| `SESSION.dat` | the format converters ([`ndm_plx2dat`](../commands/ndm_plx2dat.md), [`ndm_aom2dat`](../commands/ndm_aom2dat.md), `ndm_ncs2dat`, `ndm_smr2dat` — see [format conversion](../commands/format-conversion.md)), [`ndm_mergedat`, `ndm_concatenate`](../commands/channel-manipulation.md) | wide-band recording |
| `SESSION.fil` | [`ndm_hipass`, `ndm_bandpass`](../commands/signal-processing.md) | high-pass / band-pass filtered copy of the input (default `SESSION.dat`); same rate and channels. Spike detection and extraction read it. |
| `SESSION.lfp` | [`ndm_lfp`](../commands/signal-processing.md) | low-pass filtered and downsampled to the LFP rate |
| `SESSION.eeg` | older pipelines | legacy name for `.lfp`; NeuroScope still opens it |
| `SESSION-spkclean.dat` | [`ndm_stripdat`](../commands/ndm_stripdat.md) | `.dat` with spike waveforms subtracted; `ndm_lfp` uses it automatically when present |
| `SESSION-emgclean.dat` | `ndm_extractemg` | `.dat` with the EMG component removed, plus the link `SESSION.emgclean` → `SESSION-emgclean.dat` |
| `SESSION.px<stream>` | [`ndm_plx2dat`](../commands/ndm_plx2dat.md) | extra Plexon continuous streams; see [`.px<stream>`](pxstream.md) |
| `SESSION.<ext>.slice.<tag>` | [`ndm_slicebinary`](../commands/signal-processing.md) | a channel subset (and optional time window) of `SESSION.<ext>`; same layout with the slice's own channel count, described by a `.info` sidecar |

`.dat`, `.fil`, `.lfp` and `.eeg` are **SessionWide** types in the
[naming convention](naming.md): `<base>.<type>`, no method, no group. The other
names above are not in the custody tables; they are only ever opened by explicit
path.

## Reading another file through `inputExtension`

The steps that read a signal file take the name `SESSION.<inputExtension>`
from their `inputExtension` parameter (see [parameters](../parameters.md)):

| Step | Default input |
|---|---|
| `ndm_hipass`, `ndm_bandpass`, `ndm_lfp` | `dat` |
| `ndm_slicebinary` | `lfp` |
| `ndm_detectspikes`, `ndm_extractspikes` | `fil` |

Any file in the layout above can be named, as long as it has the session's
channel count (all of these steps take `nChannels` from `acquisitionSystem`).
Examples:

- `inputExtension: emgclean` reads the EMG-cleaned recording through the
  `SESSION.emgclean` link that `ndm_extractemg` creates.
- `inputExtension: dat.slice.shank0` reads a slice — only by steps that are
  given the slice's channel count, so in practice a slice is analysed outside
  the pipeline.
- `inputExtension: pxfp` reads a Plexon stream — only meaningful when that
  stream has the `.dat`'s channel count and the step does not depend on the
  sampling rate (each stream has its own rate).

The input is used as given: nothing checks that its sampling rate or channel
count match the session.

## Readers

- **NeuroScope** displays any of these files with the session's channel count
  and sample width. `.dat` uses the acquisition rate and `.eeg` the LFP rate;
  every other extension (`.lfp`, `.fil`, `.px<stream>`, …) takes its rate from
  the session YAML `files:` entry for its last extension token, and NeuroScope
  asks for one when there is none.
- **`neurofileio`** (`datSampleCount`, `readDatWindow`) reads any `int16`
  interleaved file regardless of extension.
- **Spike engines** (`process_extractspikes*`) read the input through a
  temporary `.fil` link; `process_spikefilter.py` reads the same source to
  check raw windows.

## See also

- [Signal processing](../commands/signal-processing.md) — `ndm_hipass`,
  `ndm_bandpass`, `ndm_lfp`, `ndm_slicebinary` (slice naming and the `.info`
  sidecar).
- [Spike detection](../commands/spike-detection.md) — what reads the `.fil`.
- [`.res`](res.md), [`.spk`](spk.md) — spike times and waveforms cut from the
  `.fil`.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
