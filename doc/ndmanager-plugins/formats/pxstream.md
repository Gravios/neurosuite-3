# `.px<stream>` — extra Plexon continuous streams (binary)

[`ndm_plx2dat`](../commands/ndm_plx2dat.md) (`process_plxconvert`) exports one
continuous stream of a Plexon recording as the `.dat` and can write each other
stream to its own file: `SESSION.pxai` (auxiliary analog inputs),
`SESSION.pxfp` (field potentials), `SESSION.pxspkc` (spike-filtered
continuous), …

## Layout

The [`.dat`](dat.md) layout: little-endian `int16`, sample-major interleaved
(`value(sample t, channel c)` at `t × nChannels + c`), no header. Values are the
raw counts Plexon stored; µV = count × `channels[].uvPerCount`.

Each stream keeps its own channel count, sampling rate and sample grid. They are
recorded in the session metadata under `sourceRecording.extraStreams[]` (one
entry per file):

| Property | Field |
|---|---|
| file, extension | `.file`, `.extension` |
| channel count and order | `.nChannels`, `.channels[]` (header order; channels without data are zero-filled unless `keepEmpty: false`) |
| sampling rate | `.samplingRate_hz`, `.ticksPerSample` |
| length | `.nSamples`, `.duration_s` |
| time of sample 0 | `.firstSampleTimestamp_ticks` (Plexon clock) and `.offsetFromDat_s` (seconds after `.dat` sample 0; ≥ 0 and under one sample of this stream, negative only for a stream that starts before the `.dat`) |
| pauses | zero-filled; `.nSegments`, `.gaps[]` |
| listed for NeuroScope | `.listedInSessionFiles` |

## Naming

The extension is `px` followed by the stream's channel-name prefix in lower
case, letters and digits only (`AI` → `pxai`). The `px` prefix keeps it clear of
every neurosuite type (`dat`, `lfp`, `fil`, `eeg`, …); it is not in the custody
type tables ([naming](naming.md#files-outside-the-type-tables)).

Which streams are written is set by `extraStreams` on `ndm_plx2dat`: `auto`
(default — every other stream with data except `SPKC`, the spike-filtered copy of
the wide-band stream), `all`, `none`, or a comma list of prefixes (`AI,FP`).

## Readers

- **NeuroScope** reads a file with the session's channel count and the sampling
  rate listed for its extension under the session YAML's `files:`.
  `ndm_plx2dat` lists a `.px<stream>` there only when its channel count equals
  the `.dat`'s (`listedInSessionFiles: true`); the others are still written and
  described in `sourceRecording`.
- **Pipeline steps** can read one through `inputExtension` (e.g.
  `inputExtension: pxfp`) under the same condition — they use the session's
  channel count — and only where the step does not depend on the sampling rate;
  see [`.dat`](dat.md#reading-another-file-through-inputextension).

## See also

- [`ndm_plx2dat`](../commands/ndm_plx2dat.md)
- [`.pxres` / `.pxclu` / `.pxspk`](pxspikes.md) — Plexon's online spikes.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
