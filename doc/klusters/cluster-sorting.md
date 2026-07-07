# Cluster sorting and reordering

**Actions → Sort Clusters** collects the operations that change the
*order* of clusters. They fall into two families:

1. **Reorder by Similarity** — a matrix-driven seriation that places
   similar clusters next to each other.
2. **Standalone sorts** — renumber clusters by a single scalar per cluster
   (spike count, time, contamination, …).

All of these preserve clusters `0` (artefact) and `1` (noise) at the
front. Except where noted, they **renumber** clusters (a single undoable
step that rewrites the `.clu` IDs and updates every view, the palette, and
the curation log via `reorderClustersByPermutation`).

## Reorder by Similarity (`Shift+S`)

Orders the non-special clusters so that similar ones are adjacent. Two
preferences (Preferences → **Sorting**) control it.

### Algorithm — `reorderMethod`

| Value | Method | Basis |
|---|---|---|
| `0` *(default)* | Single-linkage (MST) | Seriation of the error/template similarity matrix by a minimum-spanning-tree leaf order. |
| `1` | Spectral (Fiedler) | Orders by the Fiedler vector of the similarity Laplacian — a **global** objective, so the whole layout (not just local neighbours) respects the similarity structure. |
| `2` | Feature-space (PC1) | Orders by the first principal component of the clusters' feature-space centroids. Reads the `.fet` directly; needs no matrix. |

Methods 0 and 1 need a computed error (or template) matrix in the active
display; if it is stale they recompute once and re-run automatically.

### Scope — `reorderDisplayOnly`

| Value | Effect |
|---|---|
| `false` *(default)* | **Renumber**: cluster IDs are rewritten into similarity order; every view, the palette, and the `.clu` follow. |
| `true` | **View-only**: the error-matrix rows and the (parent) cluster palette are re-laid-out into similarity order **without** changing any cluster ID. Nothing is written to `.clu`. Applies to the parent palette only; child palettes keep their natural order. |

## Standalone sorts

| Menu entry | Orders clusters by |
|---|---|
| **Sort Clusters by Spike Count** | Descending spike count (largest becomes `2`). |
| **Sort Clusters by Time** | Ascending starting-edge time (earliest-firing becomes `2`). |
| **Sort Clusters by Contamination** | Descending refractory (ISI-violation) contamination. |
| **Sort Clusters by SNR** | Signal-to-noise ratio of the mean waveform. |
| **Sort Clusters by Error p-value** | The error-matrix confusion structure (needs a computed error matrix). |
| **Sort by Residual (Gated by Count)** | The residual-matrix separability, gated by spike count (needs a computed residual matrix). |
| **Sort by Nearest-Neighbour Waveform** | A greedy nearest-neighbour chain over per-sample **median** waveforms (see below). |
| **Sort by Waveform (Global / Spectral)** | Spectral (Fiedler) seriation of the median-waveform similarity (see below). |

## Waveform sorts: local vs global

The two waveform sorts share the same input — each cluster's per-sample
**median** waveform (read from `.spk`, robust to the odd artefact spike
in a way a mean is not) — and differ only in how they order it. Both are
matrix-free (no error/template matrix required) and run under a wait
cursor.

- **Nearest-Neighbour Waveform** walks a greedy chain: start at one
  cluster, then repeatedly step to the nearest not-yet-placed cluster by
  Euclidean waveform distance. This is **local** — each step is locally
  optimal, but with no global objective the chain can jump when a local
  neighbourhood is exhausted.
- **Waveform (Global / Spectral)** orders by the Fiedler vector of the
  waveform-similarity Laplacian (similarity = `dmax − distance`). This
  minimises a **global** objective, so the whole arrangement reflects
  which groups of clusters sit where and avoids the greedy chain's
  discontinuities.

As a rule of thumb: the greedy chain preserves exact nearest-neighbour
adjacencies; the spectral sort gives a more globally coherent layout.
Cost is dominated by reading the spikes (`O(total_spikes × waveform
length)`); the distance matrix and seriation are `O(N² …)` in the cluster
count `N`, so the spectral/NN sorts are best on moderate `N`.

---

*Part of the [klusters](README.md) documentation.*
