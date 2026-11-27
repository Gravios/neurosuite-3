# `.spkx.<method>.N[.stage]` — extended records of rows the recording does not hold (binary)

A row of the [`.spk`](spk.md) is the window of the recording at the row's
[`.res`](res.md) time. Every tool that moves a spike — Klusters' realignment,
nudge, *Re-extract spike waveforms on save*, a feature-space sync — cuts the row
again from the recording at its new time.

Some rows are not windows of the recording. A row added by Klusters' **Add
Residual Spikes as Clusters** is the residual of a collision row: the window at
its time *less* the collision row's own unit. Cut again from the recording, it
would be the mixed waveform it was taken out of. The `.spkx` holds such a row's
record over a **wider** window — the `.spk` window and `margin` samples either
side, in the `.spk`'s own space (raw or stderiv) — so that it can be cut again at
any time within ±`margin` of the time it was made for, the way the recording
would be.

**Naming.** Per `.spk` method and per stage, like the `.spk`:
`<base>.spkx.<method>.<group>[.<stage>]`.

**Writers / readers.** `neurofileio::readSpkx` / `writeSpkx` / `spkxFind` /
`spkxCut` in `libneurosuite-core`. Klusters writes it with every stage Add
Residual Spikes makes (carrying the rows of the stage it was made from), and
reads it wherever it would cut a row from the recording. Sort Spikes by Time
reorders it with the rows. fiber-kit does not read or write it.

## Layout

Little-endian:

```
Header (32 B)
  magic      4 B    'S' 'P' 'X' 0x01
  nRows      u32    rows held here
  nSamples   u32    samples per channel of a .spk record (the window)
  nChannels  u32    channels of the group
  nSpikes    u32    rows of the .spk this belongs to
  group      u32    1-based spike group
  margin     u32    samples either side of the window
  flags      u32    reserved (0)
Index
  nRows × int64     0-based .spk rows, strictly ascending
Times
  nRows × int64     the time each record is centred on: the row's .res time
                    when it was made
Sources
  nRows × int64     the row it was made from (the collision row), or -1
Body
  nRows × (nSamples + 2 margin) × nChannels int16, sample-major,
  channel-fastest, in index order
```

For a row now at time `t` with `|t − time| ≤ margin`, its `.spk` record is
samples `[t − time + margin, t − time + margin + nSamples)` of its body record
(`spkxCut`). Beyond the margin there is none: a tool keeps the row where it is.

The extended record is what the window would be, cut over the wider span and
with the same subtraction: a stderiv record's temporal difference inside the
span reads the sample before it, as a seeded window does, so the slice at the
row's own time is the `.spk` record (bar the first sample of an unseeded
session, which the realignment's check ignores).

`readSpkx` rejects a bad magic or version, a file shorter than its header says,
an index that is not strictly ascending or names a row at or beyond `nSpikes`,
and a source at or beyond `nSpikes`. `writeSpkx` refuses the same and tables of
the wrong length.
