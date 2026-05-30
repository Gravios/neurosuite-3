# Cluster curation

Operational guide for working through a `.clu.N` in Klusters. For
the program reference (every shortcut, view, and option), see
[`../klusters/README.md`](../klusters/README.md). This page is the
practical recipe — what to look at, in what order, and which tool to
reach for.

## Opening

Either open the session YAML and pick a group:

```sh
klusters /data/session/session.yaml
```

…or open a `.fet.N` directly:

```sh
klusters /data/session/session.fet.7
```

Klusters auto-detects pipeline variant (raw vs stderiv) and remembers
the pick for the rest of the session.

## A reasonable first pass

For an unfamiliar `.clu.N`, this is the order most curators settle
into:

### 1. Triage with the cluster overview

Scan the [Overview Display](../klusters/README.md#overview-display) and
the [Correlogram Display](../klusters/README.md#correlogram-display).
Look for:

- Refractory-period violations in autocorrelograms (under ~1 ms is
  almost always noise or a contaminated cluster).
- Bimodal cross-correlograms between adjacent clusters (a sign of one
  unit accidentally split in two).
- Clusters with very few spikes (consider sending to noise/MUA).

Use `R` to renumber clusters once you've removed obvious junk.

### 2. Fix obvious splits and merges

For visibly bimodal clusters, try DipSplit first (`D`) — it tests
bimodality on the top PCs and only splits when the test is clear. If
DipSplit doesn't fire but you're confident it's bimodal, draw a lasso
in the scatter and use `S` (split) or `C` (new cluster).

For clusters that look multi-modal in a specific feature pair (three
or more density blobs in the active scatter view), try Watershed
(`W`). It runs a 2D density watershed on the selected clusters using
the active view's X/Y dimensions and creates one new cluster per
density basin. Pick the projection where the modes are most clearly
separated *before* pressing `W` — watershed sees only the active
scatter view's two dimensions. A live-preview dialog lets you adjust
smoothing and peak-height thresholds before committing.

DipSplit and Watershed are complementary:
- **DipSplit**: cluster-by-cluster, statistical bimodality test on
  top PCs, conservative (won't split if the test is ambiguous).
- **Watershed**: density-based segmentation across a user-chosen 2D
  projection, handles 3+ basins in one action, dissolves all
  selected source clusters into the new basin clusters plus a
  residual catch-all.

For clusters that look like one unit fragmented across two IDs:
select both in the palette, press `G` (group/merge). The
[Template Matrix Display](../klusters/README.md#template-matrix-display)
makes near-duplicates obvious.

For larger over-split situations — say a unit fragmented across 5+ IDs
that visibly cluster together in the template matrix — use **Auto-Merge**
(`Shift+G`, or Action → "Auto-Merge Similar Clusters..."). Auto-Merge
runs pairwise template cross-correlation across the selected clusters
(or all active clusters, in the *All active* scope) and proposes merge
groups by union-find on pairs scoring at or above the threshold
(default 0.98, matching KKE's offline `WithinChunkTemplateMatch`). A
preview dialog lists the proposed groups with a checkbox per group so
you can accept or reject each before committing. See
[Auto-Merge action](../klusters/README.md#auto-merge-action) for the
full reference.

Auto-Merge complements manual `G` group-merge:

- **`G` (group)**: explicit, you've already identified the duplicates
  in the palette or template matrix. No threshold; merges whatever you
  selected.
- **`Shift+G` (Auto-Merge)**: lets klusters identify candidate groups
  by template similarity. You review the proposals, accept or reject
  each. Better when there are many near-duplicates to find than to
  pick out manually.

### 3. Realign

```
Actions → Realign Spikes  (Shift+L)
```

Recomputes spike timestamps so each spike's peak sits at the same
sample offset within its waveform. Use this whenever clusters look
"smeared" in the waveform display. See
[Spike realignment](../klusters/README.md#spike-realignment) for the
algorithm; results are in a `.pending` sidecar so you can reject.

### 4. Recluster

```
Actions → Recluster (KiloKlustaKwik)  (Shift+R)
```

Runs KiloKlustaKwik on the selected cluster(s) only. Useful when one
cluster contains visible substructure that DipSplit + lasso splitting
hasn't resolved. The
[Automatic Reclustering](../klusters/README.md#automatic-reclustering-kiloklustakwik)
section covers the parameter set.

### 5. Annotate

In the cluster info panel: set quality (Good / Uncertain / Bad) and
type (pyramidal / interneuron / etc.). **This is the step most
curators skip but it's the most leveraged** — quality annotations
gate which clusters feed [empirical-prior training](empirical-priors.md)
and downstream analyses.

### 6. Save

`Ctrl+S` writes `session.clu.N` and appends to
`session.curation_log.N.jl`. The log is one JSON-line record per
editing operation; see [Curation logging](../klusters/README.md#curation-logging)
for the schema.

## Specific situations

### Drift across the recording

If clusters appear to "tilt" over time in the scatter plot (a
spike-cloud whose centre drifts), this is electrode drift.
DipSplit, splitting, and merging won't fix this. After curation,
run [drift correction](drift-correction.md) to compute a probe-wide
drift trajectory and apply it to sibling shanks.

### One cluster contaminating its neighbour

The
[Grouping Assistant Display](../klusters/README.md#grouping-assistant-display)
computes pairwise feature-space overlap (Mahalanobis distance with
the option of GPU acceleration) and surfaces the most ambiguous
cluster pairs. Work through the top of the list; for each pair,
either merge (if the suggestion is correct) or use lasso + `A`
(send to artefact) on the bridging spikes.

### Suspected collisions

If a cluster's autocorrelogram has a peak at very short lag (~0.5 ms)
that doesn't look like a refractory violation but rather a duplicate
spike pattern, you may have collision artefacts. After the cluster is
otherwise clean, run [collision decomposition](collision-decomposition.md)
to resolve overlapping waveforms.

### Recovery after a Klusters crash

Klusters writes `session.#.clu.N` autosaves periodically. Open the
most recent autosave to recover work. The curation log is also
append-only and can be replayed manually if needed.

## When to stop

A "done" `.clu.N` typically has:

- Every Good cluster with refractory period > 1 ms and ISI CV in a
  reasonable range
- No bimodal correlograms between cluster pairs
- All small/uncertain clusters either merged into a parent, sent to
  noise/MUA, or marked Uncertain explicitly
- Quality and type annotations on every Good cluster

You don't have to perfect every Uncertain cluster — that's what the
annotation is for. Mark Uncertain, save, move on.

## See also

- [`../klusters/README.md`](../klusters/README.md) — full program reference
- [Empirical priors](empirical-priors.md) — what your curation feeds into
- [`../kiloklustakwik/README.md`](../kiloklustakwik/README.md) — automatic-clustering reference
- [Iterative refinement](iterative-refinement.md) — strip → redetect → re-sort loop
