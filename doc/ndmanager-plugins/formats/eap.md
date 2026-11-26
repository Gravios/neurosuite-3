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
editing, its **Mark Collisions** (partners found on the waveforms) and its
Save (the fitted offsets); fiber-kit's `fiber-unmix` (every kept spike's own
class and the explained detections; a table made anew by each run),
`fiber-decollide` and `fiber-envelope` (cells added to the stage they
analyse), `fiber-spkc --write-offsets`, and `fiber-unmix-children` (a copy of
the stage's table under `<tag>_h`). **Readers:** Klusters; fiber-kit's
`fiber-spkc`, `fiber-template --eap` and `fiber-unmix --skip-marked`.

The C++ programs go through `neurofileio::readEap` / `writeEap` in
`libneurosuite-core`; fiber-kit has its own reader and writer
(`neuro_io.read_eap` / `write_eap`). The two are held together by
`golden.eap.1`, byte-identical in both repositories: each side reads it and
writes it back.

**Which stage's table.** Klusters, the decollide commit and `fiber-spkc` read
the base matrix when the stage has none. fiber-kit's `fiber-template --eap` and
`fiber-unmix --skip-marked` read the stage's own only, and `fiber-decollide` /
`fiber-envelope` start a stage without one from an empty table, not from the
base matrix.

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

`writeEap` (and `writeTcl`, `writeSpkc`) writes a temporary file beside the target and renames it
over it: a reader never sees half a table, a failed write leaves the old one, and a second hard
link of the old file keeps the old contents. A symbolic link is followed — the file it names is
replaced and the link stays. fiber-kit's writers do the same.

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

## Offsets and spike times

An offset is relative to the row's `.res` time, and to where the class's unit puts its own spikes
in their windows.  A tool that moves spike times has to keep the offsets in step: Klusters does it
at Save, writing for every collision row whose partner it could fit the whole-sample offset that
fit found (`KlustersDoc::writeCollisionOffsets`); between saves it carries the difference in memory.

There is one `.res` per group and one `.eap` per stage. A realign saved from one stage moves the
times every stage reads, and only the open stage's offsets are rewritten: the other stages' tables
(and the base one) then hold offsets for the old times. Nothing records which times a table
holds for.

## What a class is, across stages

A class is a column, and the registry is shared by the stages. A writer finds a unit's class by
its `provenance_clu` alone, so cluster 5 of one stage and cluster 5 of another sort are given the
same column. Within one stage's table a column is one unit; across stages it need not be. Readers
do not rely on it: which cluster a class is NOW is read off the stage's own membership
(`decollide_fit::homeClusters`), with the registry's provenance as the fallback for a class that
no row names alone.

A spike moved to another cluster keeps the class it named: no tool renames it. Klusters' Mark
Collisions renames it for the rows it marks.

## Size changes

- When a writer needs a class and no `.tcl` slot is `free`, the matrix is
  widened (by `nCells`, default 128, columns); existing cells keep their
  positions, new columns are absent, and the `.tcl` grows to match.
- `nSpikes` must equal the group's current spike count. After the spike set
  changes (e.g. re-detection added spikes), `ndm_decomposecollisions` skips
  the `.eap` update; re-create the matrix with `process_initeap --overwrite 1`.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
