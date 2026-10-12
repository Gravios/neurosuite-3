# `.px<stream>` — extra Plexon continuous streams (binary)

Written by [`ndm_plx2dat`](../commands/ndm_plx2dat.md) (`process_plxconvert`)
for every continuous stream of a Plexon recording other than the one exported
as the `.dat` — `session.pxai` (auxiliary analog inputs), `session.pxfp`
(field potentials), `session.pxspkc` (spike-filtered continuous), … The
extension is `px` followed by the stream's channel-name prefix in lower case;
the `px` prefix keeps it clear of every neurosuite type (`dat`, `lfp`, `fil`,
`eeg`, …) and of custody's type tables.

## Layout

Identical to the [`.dat`](dat.md): little-endian `int16`, interleaved,
sample-major (`value(sample t, channel c)` at `t × nChannels + c`), no header.
Values are the raw counts Plexon stored; µV = count ×
`sourceRecording.extraStreams[].channels[].uvPerCount`.

| Property | Where it is recorded |
|---|---|
| channel count and order | `sourceRecording.extraStreams[].nChannels`, `.channels[]` (header order; channels without data are zero unless `keepEmpty: false`) |
| sampling rate | `.samplingRate_hz` |
| time of sample 0 | `.firstSampleTimestamp_ticks` (Plexon clock) and `.offsetFromDat_s` (seconds after `.dat` sample 0; ≥ 0 and below one sample of this stream, negative only for a stream that starts before the `.dat`) |
| pauses | zero-filled; `.gaps[]` |

## NeuroScope

NeuroScope looks up a file's sampling rate in the session YAML's `files:`
list by its last extension token and reads it with the session's channel
count. `ndm_plx2dat` therefore lists a `.px<stream>` file under `files:` only
when its channel count equals the `.dat`'s; other files are still written and
described in `sourceRecording`.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
