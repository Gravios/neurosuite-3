# First-time spike sort

Walks a fresh recording from raw acquisition through to a
manually-curated `.clu.N`. Assumes you have a session YAML — for
new recordings, ndmanager generates one from a template; see
[`../ndmanager/README.md`](../ndmanager/README.md).

## Steps

### 1. Acquire and convert

Convert the acquisition file to neurosuite format. For AlphaOmega:

```sh
cd /data/recording_001
ndm_aom2dat recording_001.yaml
```

Produces `recording_001.dat` (interleaved int16) and a populated
`recording_001.yaml` with calibrated per-group KiloKlustaKwik defaults.
For other systems, see [format conversion](../ndmanager-plugins/commands/format-conversion.md).

### 2. Preprocess

Run the standard preprocessing chain:

```sh
ndm_hipass         recording_001.yaml                   # → .fil
ndm_lfp            recording_001.yaml                   # → .lfp
ndm_extractspikes  recording_001.yaml                   # → .res.standard.N, .spk.standard.N (shared, raw)
ndm_pca            recording_001.yaml --method stderiv  # → .fet.stderiv.N, .pca.stderiv.N
```

For the raw (`standard`) variant, drop `--method stderiv` (standard is
the default) and add `ndm_spikecleaner` + `ndm_denoiseuniform` before PCA. See
[spike detection](../ndmanager-plugins/commands/spike-detection.md)
for the trade-offs.

### 3. Automatic clustering

```sh
ndm_klustakwik recording_001.yaml
```

Runs KiloKlustaKwik for every spike group sequentially (groups are
sequential, but KiloKlustaKwik itself is multi-threaded across chunks and
runs). Output: `recording_001.clu.N` per group.

For details on the chunked-CEM pipeline, GPU dispatch, and Phase 2.5
subspace reclustering, see
[`../kiloklustakwik/README.md`](../kiloklustakwik/README.md).

### 4. Curate manually

Open one shank at a time:

```sh
klusters recording_001.fet.1
```

Or open the session YAML and pick a group. Standard editing
operations (split, merge, recluster, realign, nudge, dipsplit) are
documented in the [cluster-curation workflow](cluster-curation.md).

**Annotate cluster quality as you go.** Mark Good / Uncertain / Bad
in the cluster info panel — this gates which clusters are eligible
for [empirical-prior training](empirical-priors.md) later.

Save with `Ctrl+S`. Klusters writes
`recording_001.clu.N` and appends to
`recording_001.curation_log.<group>.<method>`.

Repeat for each shank.

### 5. Validate

Open the session in NeuroScope to overlay clusters on the LFP and
event traces:

```sh
neuroscope recording_001.yaml
```

Look for clusters whose firing pattern lines up with expected event
markers (LFP ripples, behavioural events, optogenetic stimulations).
Suspect anything that fires uniformly without modulation — likely
noise or MUA.

## Next steps

After a clean first sort, you typically want:

- **[Re-extract at a lower threshold](re-extract-lower-threshold.md)** —
  recover weak units missed in step 2.
- **[Empirical priors](empirical-priors.md)** — once you have a few
  curated sessions on the same probe, train per-shank priors so
  `ndm_subcluster_unmatched` has better defaults.
- **[Iterative refinement](iterative-refinement.md)** — subtract
  curated waveforms and re-sort the residual.
- **[Drift correction](drift-correction.md)** — for chronic recordings
  spanning hours.

## See also

- [`ndm_aom2dat`](../ndmanager-plugins/commands/ndm_aom2dat.md)
- [`ndm_klustakwik`](../ndmanager-plugins/commands/ndm_klustakwik.md)
- [`../klusters/README.md`](../klusters/README.md)
- [Pipeline overview](../ndmanager-plugins/pipeline.md)
