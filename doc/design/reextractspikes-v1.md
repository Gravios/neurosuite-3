## ndmanager-plugins — reextractspikes + shadow clustering (2026-04-18)


> *Historical design record. Describes the retired `.spkD` / `.fetD` / `.pcaD` naming; the current scheme is the [variant convention](../ndmanager-plugins/formats/naming.md).*

New two-stage pipeline for second-pass spike detection.  Re-runs
threshold detection with a reduced threshold while masking out timestamps
already present in `$session.res.N`, then assigns each newly-detected
spike to the "shadow" of its nearest existing cluster by robust
Mahalanobis distance in feature space.  Shadow cluster IDs land directly
in `$session.clu.N`; no parallel stem.

### process_reextractspikes (new)

Second-pass detector.  Scans `$session.fil` via `mmap` (no chunked
buffer state machine — the simplified I/O model is appropriate because
the refractory + peak-search windows are small relative to file length).
Rejects any candidate peak within `± maskHalfWidth` samples of a
timestamp in the per-group mask `.res.N`.  Per-group detection runs in
parallel via OpenMP.  Thresholds are user-supplied (`-t`); the bash
wrapper computes them by scaling `process_medianthreshold`'s baseline
by `reextractThresholdFactor` (default 0.75).  Writes `.res.N` (int64)
and `.spk.N` (int16, sample-major `[s*nChanGroup + c]`) at the supplied
output stem.

### process_reextractspikes_stderiv (new)

Spatial-derivative + temporal first-difference variant, mirroring
`process_extractspikes_stderiv`.  Thresholds are computed internally
from the stderiv signal using the Quiroga (2004) robust estimator
(`thr = factor × 4 × median(|stderiv|) / 0.6745`) over the
`-B startByte / -Z sizeBytes` window.  Waveform extraction (Pass 2)
writes RAW ADC amplitudes under the `.spkD.N` extension so downstream
tools see unmodified waveforms.  The bash wrapper renames `.spkD.N →
.spk.N` before passing to `process_shadowcluster`, which expects the
standard extension.

### process_shadowcluster (new)

Classifier + merger.  For each existing cluster `k` with size ≥
`minClusterSize` (default 50) in the reference `.fet.N / .clu.N`,
computes a robust per-dimension centroid (median) and scale
(MAD × 1.4826; the 1.4826 Gaussian-consistency factor converts MAD into
an unbiased σ estimator) in the `nDims-1` non-timestamp feature
subspace.  Clusters 0 and 1 (artefact / MUA per neurosuite convention)
are never parents.  Projects each new waveform through the reference
`.pca.N` (supports both the 5-int32 legacy header written by
`process_pca` and the magic-prefixed 7-int32 PCAE header expected by
`process_refeaturize`; auto-detects `-x` extra-features mode from the
`.fet` dimensionality) and assigns to the shadow of the argmin-
Mahalanobis parent whenever the squared distance
`d² = Σ_d ((f[d] - μ_k[d]) / σ_k[d])²` is below a
`χ²(nFeatDim, reextractChi2)` cutoff (default 0.9999).  Spikes that do
not fit any parent go to a single unmatched bin.  χ² inverse CDF is
computed via a Wilson–Hilferty approximation (~0.1% accuracy for k≥5),
avoiding a GSL dependency.

Shadow cluster IDs are assigned as `shadow(k) = maxExistingId + 1 + k`,
unmatched as `maxExistingId + 1 + maxExistingId + 1` — so shadow IDs
never collide with existing ones.  The merge is in-place by default
(`--out` defaults to `--ref`) and is crash-safe: each merged output is
written to a `.tmp` file and renamed atomically on completion.

### ndm_reextractspikes / ndm_reextractspikes_stderiv (new bash drivers)

Orchestrators.  Read the session YAML for all parameters via
`read_script_parameter`; fall back to hardcoded defaults (kept in sync
with the XML descriptor `<value/>` tags) only if the session was
generated before these plugins shipped.  `cp` originals to
`$session.<ext>.<N>.bak` on first run (skip if the `.bak` already
exists — prior backup is preserved), then run `process_shadowcluster`
in-place.

### templates/template.yaml + template.xml

Added `ndm_reextractspikes` and `ndm_reextractspikes_stderiv` parameter
blocks after `ndm_redetectspikes`.  Populates new sessions with the
five reextract-* parameters (`reextractThresholdFactor=0.75`,
`reextractMaskHalfWidth="" (→ refractoryPeriod)`,
`reextractMinClusterSize=50`, `reextractChi2=0.9999`,
`reextractExtraFeat=auto`) so ndmanager presents them in the GUI
without any additional configuration.

### Mathematical notes

- **Diagonal (not full) covariance** is the right choice for the
  per-cluster scale matrix.  A full robust covariance would require a
  high-dimensional M-estimator (e.g. MCD), which is both computationally
  heavier and offers little benefit when the decision is ultimately
  validated by the analyst in Klusters.  The per-dimension median +
  MAD×1.4826 is the same pattern used by the Klusters cluster-filter.
- **χ²(nFeatDim, 0.9999)** as the default admission threshold matches
  the calibration established elsewhere in the toolchain (e.g. Klusters
  `filterByMahalanobis`).  For typical 24- or 32-dim `.fet` files this
  yields `d²` cutoffs of ~56 and ~68 respectively.
- **Clusters 0 and 1 excluded as parents.**  Shadow-assigning new
  spikes to "artefact" or "MUA" gives no useful information — those
  spikes are better routed to the unmatched bin for operator
  inspection.
