# `.wtl.N[.stage]` — manual template-lineage forest (versioned text)

The curator's hand-built template lineage for a spike group: a forest with one
tree per template class. Each node is a running summary (mean, standard
deviation and count) of the spikes the curator selected for it. Where
[`.wti`](wti.md) / [`.wtf`](wtf.md) are a library generated automatically from
the clustering, the `.wtl` is the editable source of a curated model.

**Naming.** Method-less, per group and per stage:
`<base>.wtl.<group>[.<stage>]` (a `.` in the stage tag is written as `_`).

**Writers / readers.** Klusters' template lineage editor
(`templatelineagestore`), through `neurofileio::readWtl` / `writeWtl` in
`libneurosuite-core` (see also
[`FILE_FORMATS.md` §4.5](../../../src/libneurosuite-core/docs/FILE_FORMATS.md)).
Committing a lineage writes the `.wtl` and renders the model: each node's
stored mean, rounded to int16, becomes one record of
`<base>.mtf.<method>.<group>[.<stage>]`, indexed by
`<base>.mti.<group>[.<stage>]` (the `.wtf` / `.wti` layouts, `.wti` version 2
with `parent` columns). No `.spk` is re-read; the generated `.wti` / `.wtf` are
not touched. A forest whose nodes are all empty placeholders is not committed.

## Format (version 2)

```
wtl 2
nSamples 42
nChannels 8
nNodes 3
# node class kind parent a b count
# mean <N> ...  then  std <N> ...   (N = nSamples*nChannels, 0 = placeholder)
node 0 3 drift-root -1 0 720 118
mean 336 12.5 14.1 ...
std 336 3.2 3.0 ...
node 1 3 adapt-leaf 0 0.0 0.5 140
mean 336 ...
std 336 ...
node 2 7 drift-root -1 0 1440 0
mean 0
std 0
```

- Line 1: `wtl <version>`; versions 1 and 2 are read, `writeWtl` always writes
  2. Blank lines, `#` comments and unknown keys are ignored.
- `nSamples`, `nChannels`: waveform geometry. `nNodes`: if present, must equal
  the number of nodes parsed, else the file is rejected.
- `node <node> <class> <kind> <parent> <a> <b> <count>`, followed by a `mean`
  and a `std` line. A node with malformed fields is skipped.

| Field | Meaning |
|---|---|
| `node` | 0-based node ID, referenced by children's `parent` |
| `class` | Template-class ID (the [`.eap`](eap.md) column) |
| `kind` | `drift-root` (summary of a time region's higher-amplitude spikes; a tree root), `adapt-leaf` (lower-amplitude, within-burst variant, child of its region's root) or `collision-leaf` (collision variant, child of its root). Other tokens are kept verbatim. |
| `parent` | Parent node ID, `-1` for a root |
| `a`, `b` | Window: start/end in seconds for drift nodes, amplitude/energy low/high for adapt nodes |
| `count` | Spikes folded into the summary; `0` = empty placeholder |
| `mean <N> v…`, `std <N> v…` | Running mean and standard deviation waveforms, `N = nSamples × nChannels` values in `.spk` element order; `N = 0` for a placeholder |

Spike indices are not stored. Adding a selection to a node folds its
(mean, std, count) into the node's with the exact parallel-variance
combination, so the original spikes are not needed afterwards.

When rendered, `kind` maps to the `.wti` `link` by prefix (`drift…` → `drift`,
`adapt…` → `adapt`, `collision…` → `collision`).

## Version 1 (read only)

Version 1 stored the spike indices on the node line:
`node <node> <class> <kind> <parent> <a> <b> <nSpikes> <spike> …`, with no
`mean`/`std` lines. The reader discards the indices and keeps
`count = nSpikes` with empty mean and std; repopulate the nodes to fill them.

> **fiber-kit note.** fiber-kit's `neuro_io` `.wtl` reader/writer still uses
> version 1. Reading version-2 files there needs a fiber-kit change.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
