# `.pxres.N` / `.pxclu.N` / `.pxspk.N` — Plexon online spikes

[`ndm_plx2dat`](../commands/ndm_plx2dat.md) (`process_plxconvert`) exports the
spikes a Plexon system detected online, with the units its online sorting
assigned: one group per Plexon spike channel that holds spikes. Disable with
`exportSpikes: false`.

Group `N` is the channel's number (`SPK01` → 1) when all spike channels have
distinct positive numbers, header order otherwise. The mapping and per-group
geometry are in `sourceRecording.onlineSpikes.groups[]`.

## Layout

Row `i` of the three files is the same spike. Spikes are in ascending time
order (re-sorted if Plexon's file was not; `groups[].reordered`).

| File | Format |
|---|---|
| `.pxres.N` | **Text**, one integer per line: the `.dat` sample index `round((tick − firstSampleTimestamp_ticks) / ticksPerSample)`. Exact for a `.dat` exported from the wide-band stream. |
| `.pxclu.N` | **Text**: first line max id + 1, then one id per spike, **Plexon unit + 1** — unsorted (0) → 1, the `.clu` multi-unit id; unit a → 2, b → 3, … |
| `.pxspk.N` | **Binary**, the [`.spk`](spk.md) layout: per spike `nSamples × nChannels` little-endian `int16`, sample-major, channel fastest (Plexon's waveform-major multi-waveform spikes are transposed). Raw counts; µV = count × `groups[].uvPerCount`. Geometry in `groups[].nSamples`, `.nChannels`. |

Spikes outside the `.dat` are not written (counted in
`groups[].skippedBeforeDat` / `skippedAfterDat`; `padToZero` keeps the early
ones), so every time is a valid non-negative `.dat` sample. Spikes whose
waveform length differs from the group's first spike are dropped and reported.

The text `.pxres` is read correctly by `neurofileio`'s text/binary probe because
no time is negative. The pipeline's own spike-time readers (`ndm_extractspikes`,
`ndm_pca`, Klusters) read binary `.res` only.

## Naming

These are separate types, not `.res.<token>.N` / `.clu.<token>.N` /
`.spk.<token>.N`. `.res` is Shared in custody — one file per group, and
`resolveAny` accepts any method token — so a Plexon copy under a method token
could be picked up as the pipeline's spike times. The `px` types are not in
custody's tables and are never resolved implicitly
([naming](naming.md#files-outside-the-type-tables)).

## Using them

To view or sort Plexon's online result with the neurosuite tools, put the files
in a separate session directory as `<base>.res.<token>.N`,
`<base>.clu.<token>.N` and `<base>.spk.<token>.N`, with a spike group in that
session's YAML matching `groups[].nChannels` / `nSamples`:

- NeuroScope reads the text `.res` / `.clu` pair as is.
- Klusters and `ndm_pca` need the binary forms: convert the
  `.res` to `int64` and the `.clu` to the `int32` header + ids layout of
  [`.clu`](clu.md), then run `ndm_pca` to produce the `.fet` Klusters loads.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
