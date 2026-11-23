# `.spkc.<method>.N[.stage]` — sparse corrected waveforms (binary)

The waveforms of the few spike rows whose `.spk` record is not what a reader
should see: a **collision row** — two or more template classes in its
[`.eap`](eap.md) row — with its partners' fitted templates subtracted.

The `.spk` stays the raw cut of the recording at the `.res` times. Every tool
that re-cuts it keeps working, and a row can never be subtracted twice. A
reader that wants the corrected waveform of row *i* takes it from the `.spkc`
when the row is listed there, and from the `.spk` otherwise.

**Naming.** Per method and per stage, like the `.spk` it overlays:
`<base>.spkc.<method>.<group>[.<stage>]`.

**Writers / readers.** `neurofileio::readSpkc` / `writeSpkc` / `spkcFind` in
`libneurosuite-core`; the subtraction itself is `core/decollide_fit.hpp`.
Klusters builds the rows when a session opens and writes the file at Save
(*Preferences > Cluster view > Subtract the partner from collision rows*);
fiber-kit follows.

## Layout

Little-endian:

```
Header (32 B)
  magic      4 B    'S' 'P' 'C' 0x01
  nRows      u32    rows held here
  nSamples   u32    samples per channel of a record
  nChannels  u32    channels of the group
  nSpikes    u32    rows of the .spk this overlays
  group      u32    1-based spike group
  flags      u32    reserved (0)
  pad        4 B
Index
  nRows × int64     0-based .spk rows, strictly ascending
Body
  nRows × nSamples × nChannels int16, the .spk record layout
  (sample-major, channel-fastest), in index order
```

`readSpkc` rejects a bad magic or version, a file shorter than its header
says, and an index that is not strictly ascending or names a row at or beyond
`nSpikes`. `writeSpkc` refuses the same.

## What a record holds

For a collision row owned (in the `.clu`) by unit A, with partner B at offset
τ in the `.eap`:

```
record = spk[i]  −  a_B · T_B(τ_B)
```

`T_B` is B's template in this method's waveform space, taken from B's rows
that are not collisions. `a_B` and the exact shift `τ_B` are **fitted** each
time the record is made (jointly with A's own template, within a few samples of
the stored τ); the whole-sample part of the fitted shift is written back to the
`.eap`. A partner whose fit is not accepted — implausible amplitude, or too
little of the residual removed — is not subtracted, and a row with no accepted
partner is not listed.

The amplitudes are not stored. The file is derived data: from the `.spk`, the
`.eap` and the clustering it can be rebuilt.

## Staleness

`nSpikes` must equal the group's spike count. After the spike set changes
(re-detection, a dedup), or after a listed row's time or a named unit has
changed without the record being redone, the file is stale and is rebuilt.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
