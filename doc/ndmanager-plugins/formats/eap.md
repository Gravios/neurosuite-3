# `.eap.N[.stage]` — EAP membership and offset matrix (binary int8)

The membership layer of the EAP template-class model: for every spike of a
group, which template classes are present in its waveform and at what offset.
It annotates the existing `.spk` rows (one row per waveform, never new rows)
and coexists with `.clu`, which stays the one-label-per-spike partition; a
collision waveform is one row with two or more classes present.

**Naming.** Method-less, per spike group and per curation stage:
`<base>.eap.<group>` is the canonical matrix; a stage adds a trailing tag,
`<base>.eap.<group>.<stage>`, and inherits the base matrix until it is first
written. The class registry [`.tcl`](tcl.md) is shared by all stages.

**Writers.** [`process_initeap`](../commands/process_initeap.md) (all-absent,
`T = nCells`, default 128);
[`ndm_decomposecollisions`](../commands/ndm_decomposecollisions.md) and the
Klusters decollide commit (collision constituents); Klusters' template-class
editing; fiber-kit's `fiber-decollide`. **Readers:** Klusters, fiber-kit's
`fiber-template --eap`. All go through `neurofileio::readEap` / `writeEap`
in `libneurosuite-core`.

## Layout

Little-endian:

```
Header (32 B)
  magic     4 B    'E' 'A' 'P' 0x01
  nSpikes   u32    rows: spikes in the group
  nClasses  u32    columns: T, the number of template-class slots
  group     u32    1-based spike group
  flags     u32    reserved (0)
  pad       12 B
Body
  nSpikes × nClasses int8, row-major: cell[i][j] at byte 32 + i·T + j
```

`readEap` rejects a bad magic or a body shorter than `nSpikes × nClasses`.

## Cells

- `cell[i][j]` in [−127, +127] — class *j* is present in spike *i*; the value
  is the integer-sample offset of class *j*'s waveform relative to spike *i*'s
  `.res` time. `0` is a valid offset (a spike's own class reads about 0).
- `−128` (`EAP_ABSENT`) — class *j* is not in spike *i*. Writers clamp offsets
  to ±127 so a real offset never equals the sentinel.

A class ID is its column index (`0 … T−1`) and stays stable when cluster IDs
change. Each column's status and provenance are in [`.tcl`](tcl.md).
Amplitudes and sub-sample shifts are not stored here; for collisions they stay
in [`.col`](col.md).

## Size changes

- When a writer needs a class and no `.tcl` slot is `free`, the matrix is
  widened (by `nCells`, default 128, columns); existing cells keep their
  positions, new columns are absent, and the `.tcl` grows to match.
- `nSpikes` must equal the group's current spike count. After the spike set
  changes (e.g. re-detection added spikes), `ndm_decomposecollisions` skips
  the `.eap` update; re-create the matrix with `process_initeap --overwrite 1`.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
