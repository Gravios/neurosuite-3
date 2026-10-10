# `.wtl.N` — manual template-lineage forest (version-tagged text)

The curator's hand-built lineage for a spike group's template library: a
**forest**, one tree per template class, that the generator re-medians into the
`.wtf` waveform stack and its [`.wti`](wti.md) index. Text, per group, the
curator-authored companion to `.wti`/`.wtf`. Read back by klusters
(`templatelineagestore`); the authoritative reader/writer is
`neurofileio::readWtl` / `writeWtl` (see
[`../../../src/libneurosuite-core/docs/FILE_FORMATS.md`](../../../src/libneurosuite-core/docs/FILE_FORMATS.md) §4.5).

```
wtl 2
nSamples 42
nChannels 8
nNodes 3
node 0 3 drift-root   -1 0   720  118
mean 336 ...        # nSamples*nChannels floats (0 = placeholder)
std  336 ...
node 1 3 adapt-leaf    0 0.0 0.5  140
mean 336 ...
std  336 ...
node 2 7 drift-root   -1 0   1440 212
mean 336 ...
std  336 ...
```

- **Header** — `wtl <version>` (versions **1 and 2** read; `writeWtl` always
  emits `wtl 2`), then `nSamples`, `nChannels`, `nNodes` (checked against the
  nodes parsed).
- **node** — `node <node> <class> <kind> <parent> <a> <b> <count>`. `parent` is
  the parent node's index, `-1` for a tree root; one tree per `class`.
- **kind** — `drift-root` | `adapt-leaf` | `collision-leaf`; unknown tokens are
  kept verbatim so the vocabulary can grow.
- **`[a,b]`** — the node's window (chunk seconds for a drift node, energy bounds
  for an adapt node), mirroring [`.wti`](wti.md)'s `row` semantics.

## Versions

- **v2 (current)** stores each node's **running `(mean, std, count)` summary**,
  not spike indices: a node folds a new selection in by an exact
  parallel-variance combine, so the originating spikes are never retained. The
  `mean`/`std` lines carry `nSamples*nChannels` floats each (all-zero = an empty
  placeholder).
- **v1 (legacy, still read)** listed spike indices per node; the reader consumes
  and discards them, mapping the node to `count = nSpikes` with empty
  `mean`/`std`.

> **fiber-kit note.** fiber-kit's `neuro_io` `.wtl` reader/writer still speaks
> **v1** (spike lists). Reading ns3's v2 running-summary files needs a separate
> fiber-kit change — never cross-applied from this repo.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
