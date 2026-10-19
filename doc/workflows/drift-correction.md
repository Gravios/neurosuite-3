# Drift correction

Estimates probe drift from one carefully curated shank, then
propagates the drift trajectory to sibling shanks on the same probe.
Use this for chronic recordings spanning hours where electrode drift
is visible as cluster centers shifting over time.

The two-step structure (estimate from curated shanks, apply to the
others) means you only have to fully curate one shank — the others
inherit its trajectory via adaptive chunk boundaries.

> **File names.** `ndm_estimatedrift` reads each group's spike files
> under their **untagged** names (`session.res.N`, `session.clu.N`,
> `session.spk.N`), not the method-tagged `.<type>.<method>.N` files
> written by the current detect/extract/sort chain. Provide untagged
> copies for the curated groups before step 2; groups without a
> `session.clu.N` are not estimated.

## Steps

### 1. Pick an anchor shank

Choose a shank where you have:

- A clean curated sort from a [first-time sort](first-time-sort.md).
- At least two well-isolated, high-firing-rate clusters spanning the
  recording duration.
- Visible drift — clusters whose centroid in feature space moves over
  time.

Drift estimation works per unit and per time window (`windowSec`,
default 60 s): it builds each unit's mean peak-to-peak amplitude on
every site of the shank and cross-correlates that depth profile with
the unit's profile in a reference window, refining the peak lag to
sub-site precision with a parabolic fit. The shank's drift in a window
is the weighted median of the unit shifts; units whose pairwise
centre-of-mass distances to the others change too much are excluded as
outliers, and a pooled multi-unit profile is used when fewer than
`minUnits` (default 3) units can be tracked. Without well-isolated,
high-firing units the result is noisy and unreliable.

### 2. Estimate drift

```sh
ndm_estimatedrift session.yaml
```

Takes only the parameter file. It estimates every spike group that has
a curated `.clu` (noise-only groups are skipped) and also writes a
probe-level average across the healthy shanks. Output: `session.drift`,
a YAML with the per-window drift trajectory of each group. It does not
overwrite an existing `session.drift`. `scipy` speeds up the
cross-correlation; without it a numpy fallback is used.

For algorithm details and parameter knobs, see
[`ndm_estimatedrift`](../ndmanager-plugins/commands/ndm_estimatedrift.md).

### 3. Inspect the drift trajectory

The `session.drift` YAML can be viewed directly. Look for:

- **Smooth monotonic trends** — typical of mechanical drift; safe to
  apply.
- **Sudden jumps** — usually electrode resets or recording artefacts;
  may indicate a bad time interval that should be excluded.
- **High-frequency oscillation** — algorithm noise; suggests the
  anchor shank's clusters aren't isolated enough or firing rates are
  too low.

If the trajectory looks unreliable, pick a different anchor shank or
consult [`../design/optimization.md`](../design/optimization.md) for
recipes that improve estimation.

### 4. Propagate to sibling shanks

```sh
ndm_applydrift session.yaml [source-group] [target-group ...]
# e.g. anchor shank 7, all other groups:
ndm_applydrift session.yaml 7
```

Uses the source group's drift trajectory to compute *adaptive chunk
boundaries* — a new chunk starts whenever the drift since the last
boundary exceeds `threshUm` (default 5 µm), with chunks at least
`minChunkSec` (default 30 s) long — and writes a `session.chunks.N` file
for each target group. The source group defaults to the YAML
`sourceGroup` (default 1); the targets default to every other group.
Each chunk then corresponds to a stable drift regime rather than a
fixed wall-clock window.

For details on adaptive chunks vs fixed chunks, see
[`../kiloklustakwik/README.md`](../kiloklustakwik/README.md) and
[`ndm_applydrift`](../ndmanager-plugins/commands/ndm_applydrift.md).

### 5. Re-sort the affected shanks

`ndm_klustakwik` does **not** read `.chunks.N` files. Use one of:

- Set `runKlustaKwik: true` (and `method`, matching your sort) in the
  `ndm_applydrift` node of the session YAML and re-run
  `ndm_applydrift`: it then sorts each target group with
  `-ChunkFile session.chunks.N`, using the `minClusters`,
  `maxClusters`, `mergeThresh`, `globalMergeIter` and `timeMergeIter`
  values of that node. (The script calls the program as `KlustaKwik`;
  this repository installs it as `KiloKlustaKwik`, so a `KlustaKwik`
  link to it must be on your `PATH`.)
- Run KiloKlustaKwik yourself with `-ChunkFile session.chunks.N`.

Clusters that previously appeared smeared across chunk boundaries
should now be tighter.

### 6. Curate

Open each affected shank in Klusters and curate as usual. Drift-
corrected sorts often reveal pairs of units that previously masked
each other because they drifted at different rates — these typically
need split-or-merge decisions during curation.

## Limitations

- Drift correction handles **continuous probe drift**. It does *not*
  handle electrode dropouts, sudden gain changes, or stage bumps —
  those need to be cut out of the recording before sorting.
- The anchor shank's drift is assumed representative of the whole
  probe. For probes whose shanks drift independently (rare but
  possible with very long shanks or heat-related expansion), each
  shank needs its own anchor: run `ndm_applydrift` once per source
  group with explicit targets.
- Drift correction is *post-hoc* — the original `.dat` and `.fil`
  aren't modified. KiloKlustaKwik gets time-varying chunk boundaries; the
  signal trace is unchanged. This is intentional, but means
  downstream waveform analyses still see the original (drifted)
  waveforms.

## See also

- [`ndm_estimatedrift`](../ndmanager-plugins/commands/ndm_estimatedrift.md)
- [`ndm_applydrift`](../ndmanager-plugins/commands/ndm_applydrift.md)
- [`.drift` format](../ndmanager-plugins/formats/drift.md)
- [`.chunks.N` format](../ndmanager-plugins/formats/chunks.md)
