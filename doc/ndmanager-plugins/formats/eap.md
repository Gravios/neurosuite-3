# `.eap.N[.stage]` — EAP membership + offset matrix (binary int8)

The multi-label membership layer of the EAP template-class model. **Method-less**,
per spike group, and per curation **stage**: `<base>.eap.<group>` is the canonical
(untagged) matrix, and a stage adds a trailing tag — `<base>.eap.<group>.<stage>` —
inheriting the base membership until it is edited. It *augments* the `.spk` (one
row per existing waveform, never new rows) and coexists with `.clu`, which stays
the dominant-label partition; a collision waveform is one `.spk` row with ≥ 2
classes set here.

Pre-constructed all-absent by [`process_initeap`](../commands/process_initeap.md)
at `T = nCells` (default 128); filled by the decollide engines
([`ndm_decomposecollisions`](../commands/ndm_decomposecollisions.md) and fiber-kit's
`fiber-decollide`) and by Klusters; read by fiber-kit's `fiber-template --eap`.

```
Header (32 B, little-endian):
  magic    {'E','A','P',0x01}
  nSpikes  u32
  nClasses u32          (= T, the number of template-class columns)
  group    u32
  flags    u32
  pad      [12]
Body:
  nSpikes × nClasses  int8, ROW-MAJOR   (cell[i][j] at i*T + j)
```

- **Cell `eap[i][j]`** = the integer-sample temporal offset of class *j*'s EAP
  within spike *i*'s window, relative to spike *i*'s `.res` time; range
  `[-127, +127]`.
- **`-128` (`EAP_ABSENT`)** = class *j* is not present in spike *i*. `0` is a valid
  offset (an EAP exactly at `res`), so a present dominant class reads ~0.
- A **template-class id is its column index** (`0 … T-1`) — stable under cluster-id
  churn by construction. Per-column lifecycle and provenance live in the companion
  [`.tcl`](tcl.md). Amplitude and sub-sample shift stay in [`.col`](col.md); `.eap`
  carries presence + integer offset only.
- Needing more than `T` live classes grows the file (every row widens; the new
  columns are absent).

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
