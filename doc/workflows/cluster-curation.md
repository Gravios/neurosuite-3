# Cluster curation

Operational guide for working through a `.clu.<method>.N` in Klusters.
For the program reference (views and options), see
[`../klusters/README.md`](../klusters/README.md). This page is the
practical recipe — what to look at, in what order, and which tool to
reach for. Only the key shortcuts are named here; **Help ▸ Keyboard
Shortcuts** (`H`) lists the live bindings, including any you have
remapped.

## Opening

Either open the session YAML and pick a group:

```sh
klusters /data/session/session.yaml
```

…or open a feature file directly (`.fet.<method>.N`):

```sh
klusters /data/session/session.fet.stderiv.7
```

Klusters auto-detects pipeline variant (raw vs stderiv) and remembers
the pick for the rest of the session.

## A reasonable first pass

For an unfamiliar `.clu.<method>.N`, this is the order most curators settle
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

For visibly bimodal clusters, try DipSplit first (`Shift+D`) — it tests
bimodality on the top PCs and only splits when the test is clear. If
DipSplit doesn't fire but you're confident it's bimodal, switch the
cluster view to a drawing tool and draw a polygon around the spikes:
`Ctrl+1` New Cluster, `Ctrl+2` Split Clusters, `Ctrl+3` Delete
Artifact, `Ctrl+4` Delete Noise, `Ctrl+5` Multi-Lasso (keep several
lassos, then `Return` splits by all of them).

For clusters that look multi-modal in a specific feature pair (three
or more density blobs in the active scatter view), try Watershed
(`Shift+W`). It runs a 2D density watershed on the selected clusters using
the active view's X/Y dimensions and creates one new cluster per
density basin. Pick the projection where the modes are most clearly
separated *before* pressing `Shift+W` — watershed sees only the active
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
[Template Matrix](../klusters/README.md#template-matrix)
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
Actions → Recluster → Recluster  (Shift+R)
```

Runs KiloKlustaKwik on the selected cluster(s) only. Useful when one
cluster contains visible substructure that DipSplit + lasso splitting
hasn't resolved. The
[Automatic Reclustering](../klusters/README.md#automatic-reclustering-kiloklustakwik)
section covers the parameter set.

### 5. Annotate

Right-click a cluster in the palette to open its cluster information
dialog. Its fields — structure, type, isolation distance, quality,
notes, and the atlas region fields — are free text, saved with the
clustering. Quality is not a fixed scale, but a consistent vocabulary
pays off: [`ndm_stripdat`](iterative-refinement.md) can select clusters
whose quality text matches its `qualityTags` list (default
`good,great,excellent`). Empirical-prior training does not read these
annotations.

### 6. Save

`Ctrl+S` writes `session.clu.<method>.N` and appends to
`session.curation_log.<group>.<method>`. The log is one JSON-line record per
editing operation; see [Curation logging](../klusters/README.md#curation-logging)
for the schema.

## Specific situations

### Drift across the recording

If clusters appear to "tilt" over time in the scatter plot (a
spike-cloud whose center drifts), this is electrode drift.
DipSplit, splitting, and merging won't fix this. After curation,
run [drift correction](drift-correction.md) to compute a probe-wide
drift trajectory and apply it to sibling shanks.

### One cluster contaminating its neighbor

The
[Grouping Assistant Display](../klusters/README.md#grouping-assistant-display)
computes pairwise feature-space overlap (Mahalanobis distance with
the option of GPU acceleration) and surfaces the most ambiguous
cluster pairs. Work through the top of the list; for each pair,
either merge (if the suggestion is correct) or draw around the
bridging spikes in Delete Artifact (`Ctrl+3`) or Delete Noise
(`Ctrl+4`) mode. To move whole selected clusters instead, use
`Shift+Delete` (artefact, cluster 0) or `Delete` (noise, cluster 1).

### Suspected collisions

If a cluster's autocorrelogram has a peak at very short lag (~0.5 ms)
that doesn't look like a refractory violation but rather a duplicate
spike pattern, you may have collision artefacts. After the cluster is
otherwise clean, run [collision decomposition](collision-decomposition.md)
to resolve overlapping waveforms.

### Recovery after a Klusters crash

Klusters autosaves to a hidden rescue file next to the cluster file,
named `.` + the cluster file name + `.autosave` (for example
`.session.clu.stderiv.7.autosave`). When you next open that clustering
and the rescue file is newer than the `.clu`, Klusters asks whether to
use it; the old `.clu` is kept under a dated name. The curation log is
append-only and can be replayed manually if needed.

## When to stop

A "done" `.clu.<method>.N` typically has:

- Every well-isolated cluster with refractory period > 1 ms and ISI CV in a
  reasonable range
- No bimodal correlograms between cluster pairs
- All small/uncertain clusters either merged into a parent, sent to
  noise/MUA, or annotated as uncertain in their quality field
- Quality and type annotations on every well-isolated cluster

You don't have to perfect every uncertain cluster — note it in the
quality field, save, move on.

## See also

- [`../klusters/README.md`](../klusters/README.md) — full program reference
- [Empirical priors](empirical-priors.md) — what your curation feeds into
- [`../kiloklustakwik/README.md`](../kiloklustakwik/README.md) — automatic-clustering reference
- [Iterative refinement](iterative-refinement.md) — strip → redetect → re-sort loop
