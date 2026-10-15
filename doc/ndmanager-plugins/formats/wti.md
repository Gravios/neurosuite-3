# `.wti.N[.stage]` — template index (versioned text)

The index of a spike group's template-waveform library: one row per record of
the companion [`.wtf`](wtf.md) stacks, naming which unit, series and bin each
record is.

**Naming.** Method-less, per group and per stage:
`<base>.wti.<group>[.<stage>]`. One index is shared by all waveform methods,
since every method's `.wtf` has the same row layout. A `.` in the stage tag is
written as `_`.

**Writers.** fiber-kit's `fiber-template` and Klusters' built-in template
generation (`neurosuite::templategen::generateToFiles` in
`libneurosuite-core`, a port of `fiber-template`). Committing a manual
[`.wtl`](wtl.md) lineage in Klusters writes the same format under the model
extensions `.mti` / `.mtf` instead, so it never overwrites a generated
library. The reader/writer is `neurofileio::readWti` / `writeWti`.

## Format

```
wti 1
nSamples 42
nChannels 8
peakSample -1
sr 20000
nRows 3
# row unit link bin a b nSpikes
row 0 3 drift 0 0 720 118
row 1 3 drift 1 720 1440 95
row 2 3 adapt 0 0.0 0.5 140
```

- Line 1: `wti <version>`; versions 1 and 2 are read. Blank lines and `#`
  comments are ignored, and so are unknown keys.
- Header keys: `nSamples`, `nChannels` (the waveform geometry the headerless
  `.wtf` relies on), `peakSample` (`-1` if unknown; the built-in generator
  writes `-1`), `sr` (Hz, `0` if unknown), `nRows` (if present, must equal the
  number of rows parsed, else the file is rejected).
- A malformed `row` line is skipped.

## Rows

`row <row> <unit> <link> <bin> <a> <b> <nSpikes> [<parent>]`

| Field | Meaning |
|---|---|
| `row` | 0-based; row *i* describes `.wtf` record *i* |
| `unit` | Series owner: the cluster ID, or, when generated from the [`.eap`](eap.md) (`fiber-template --eap`), the template-class ID (`.eap` column) |
| `link` | `drift` (median per time chunk), `adapt` (median per spike-energy bin) or `collision`; other tokens are kept verbatim |
| `bin` | 0-based position within that (`unit`, `link`) series |
| `a`, `b` | Bin bounds: chunk start/end in seconds for `drift`, energy low/high for `adapt` |
| `nSpikes` | Spikes behind the median; `0` = empty placeholder (record all zeros) |
| `parent` | Version 2 only: `row` of the lineage parent, `-1` for a tree root |

`writeWti` writes version 2, with the `parent` column, only when some row has
a parent (as in lineage-rendered `.mti` files); otherwise version 1. The Python
`fiber-template` writer may append extra provenance columns (`src_clu_*`),
which the C++ reader ignores.

A class's time scope (used by Klusters' time-restricted projection) is the
union of `[a, b]` over its `drift` rows.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
