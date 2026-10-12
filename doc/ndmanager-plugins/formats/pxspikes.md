# `.pxres.N` / `.pxclu.N` / `.pxspk.N` — Plexon online spikes

Written by [`ndm_plx2dat`](../commands/ndm_plx2dat.md) (`process_plxconvert`):
the spikes a Plexon system detected online — and the units its online
sorting assigned — one group per Plexon spike channel. Group *N* is the
channel's number (`SPK01` → 1) when all spike channels have distinct
positive numbers, header order otherwise; the mapping is in
`sourceRecording.onlineSpikes.groups[]`.

The three files have exactly the byte formats of [`.res`](res.md) (text),
[`.clu`](clu.md) (text) and [`.spk`](spk.md), under **their own types**.
`.res` is classed *Shared* in custody — one physical copy per group, resolved
across method tokens — so a Plexon copy named `.res.<token>.N` would be found
in place of the pipeline's spike times. The `px` types are not in custody's
tables and are never resolved implicitly.

| File | Format |
|---|---|
| `.pxres.N` | One `.dat` sample index per line, ascending: `round((tick − firstSampleTimestamp_ticks) / ticksPerSample)`. Exact for a `WB` `.dat`. Written as text so neurofileio's text/binary probe always classifies it correctly. |
| `.pxclu.N` | First line: max id + 1. Then one id per spike: **Plexon unit + 1** — unsorted (0) → 1, the `.clu` MUA id; unit a → 2, b → 3, … |
| `.pxspk.N` | Per spike `nSamples × nChannels` little-endian `int16`, sample-major, channel fastest (Plexon's waveform-major multi-waveform spikes are transposed). Raw counts; µV = count × `groups[].uvPerCount`. Geometry: `groups[].nSamples`, `.nChannels`. |

Row *i* of the three files is the same spike. Spikes outside the `.dat` are
not written (counted in `groups[].skippedBeforeDat` / `skippedAfterDat`), so
every time is a valid `.dat` sample.

To open Plexon's sort in Klusters, copy the three files into a separate
session directory as `<base>.res.<token>.N`, `<base>.clu.<token>.N`,
`<base>.spk.<token>.N` with a matching spike group in that session's YAML.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
