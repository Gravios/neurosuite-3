# `.spkr.<method>.N[.stage]` — sparse residual waveforms (binary)

The **other** spike of a collision row. A row whose [`.eap`](eap.md) row names
two template classes holds its own unit's spike and a partner's. The
[`.spkc`](spkc.md) holds the row with the partner taken out — the own spike, to
be read in place of the `.spk` record. The `.spkr` holds what the `.spkc` leaves
out: the partner's spike, with the own unit taken out.

It is the complement of the `.spkc`, not a variant of it: a reader must never
serve a `.spkr` record as the row's waveform.

**Naming.** Per method and per stage, like the `.spk` and the `.spkc`:
`<base>.spkr.<method>.<group>[.<stage>]`.

**Writers / readers.** `neurofileio::readSpkr` / `writeSpkr` / `spkrFind` in
`libneurosuite-core`. Klusters builds the records for the waveform view's
residual display and writes the file at Save. fiber-kit does not read or write
it yet.

## Layout

Little-endian:

```
Header (32 B)
  magic      4 B    'S' 'P' 'R' 0x01
  nRows      u32    rows held here
  nSamples   u32    samples per channel of a record
  nChannels  u32    channels of the group
  nSpikes    u32    rows of the .spk this belongs to
  group      u32    1-based spike group
  flags      u32    reserved (0)
  pad        4 B
Index
  nRows × int64     0-based .spk rows, strictly ascending
Offsets
  nRows × int32     samples from the row's .res time to the record's centre
                    (positive = later)
Classes
  nRows × int32     the .eap class the record is centred on, or -1
Kinds
  nRows × int32     0 recovered, 1 detected, 2 repeated (below)
Twins
  nRows × int64     the 0-based .spk row that already holds this spike, or -1
Body
  nRows × nSamples × nChannels int16, the .spk record layout
  (sample-major, channel-fastest), in index order
```

The header and the index are the `.spkc`'s, with another magic; four tables of
one entry a row follow the index.

`readSpkr` rejects a bad magic or version, a file shorter than its header says,
an index that is not strictly ascending or names a row at or beyond `nSpikes`,
an unknown kind, and a twin that does not fit its kind (none for a recovered
spike; a row of the `.spk` other than the row itself for the others).
`writeSpkr` refuses the same, and tables that do not hold one entry a row.

## Duplicates

A residual spike is the partner's spike, and the partner's spike may be a row
already. Each record says which it is:

| kind | | twin |
|---|---|---|
| 0 **recovered** | no row of the `.spk` holds this spike: it was missed by the detection, hidden behind the row's own | −1 |
| 1 **detected** | a row already holds it. In the partner's cluster, the two rows are one collision seen from both sides; in another cluster the spike is a row, but not where the partner named says — compare the twin's cluster with the class | that row |
| 2 **repeated** | an earlier row of this file holds the same residual spike (one partner spike in the windows of two rows) | that row |

A tool that turns residual spikes into rows — to complete a unit's train — takes
the **recovered** records only; the others would add a spike a second time. A
detected twin is a row whose time is within a few samples of
`res[i] + offsets[i]` — the nearest in the partner's cluster when it has one
there, else the nearest in any cluster; the writer sets the tolerance (Klusters:
6 samples).

## What a record holds

For a collision row *i* owned (in the `.clu`) by unit A, with partner B found
τ samples after the row's own spike:

```
record = W(res[i] + τ)  −  median over j of  W(res[j] + τ)
```

`W(t)` is the window the group's `.spk` of this method would hold for a spike
at time `t` — cut from the recording (`.fil`, else `.dat`), in the method's own
domain. The `j` are spikes of A that are not collisions: near row *i* in time,
and of those the nearest to it in waveform. Their windows, cut at the same
displacement τ, hold A's spike where it lies in the window of row *i*, so their
median is what A contributes there, and the difference is B's spike with the
recording's noise.

So a record is centred on B: B's peak is at the group's peak sample, and all of
B's waveform is in the window, also the part that lies outside row *i*'s own
record. It is laid out like, and can be compared sample for sample with, a
`.spk` record of B's own spikes. The residual spike's time is
`res[i] + offsets[i]`.

One record per row, for the row's first partner. A row with no partner fitted,
or whose own unit has too few clean spikes, is not listed.

Without a recording the windows cannot be re-cut. A writer then takes them from
the `.spk` (offset 0): the row's own record less the median of its neighbours'
records. That shows the partner as far as it lies in the row's window, not
centred.

## Staleness

Derived data: from the recording, the `.eap` and the clustering it can be
rebuilt. `nSpikes` must equal the group's spike count. A record is redone when
its row's time, cluster, partner or offset has changed. An operation that
renumbers the rows (Klusters' Sort Spikes by Time) removes the file; it is
rebuilt at the next Save.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
