## ndmanager-plugins — process_subtractspikes fixes (2026-04-19)

Seven fixes to the spike-subtraction pipeline.  The headline change is
that cluster-aware subtraction is now actually happening on
modern-format `.clu` files, and that `ndm_stripdat` can now target
specific "good" clusters for the iterative strip-then-re-detect
workflow.

### Critical — `load_clu` reads binary .clu

Before: `process_subtractspikes`'s `load_clu()` opened `.clu.N` in text
mode.  neurosuite-3's KlustaKwik and Klusters write `.clu.N` as binary
(`int32` nClusters header + `nSpikes × int32` ids) since the
modernization; the text reader silently produced a one-element garbage
list or raised `ValueError` on the header bytes.  On sessions where
the list was empty, the caller's length-mismatch check fired and
silently fell through to **Layer 0 raw-waveform subtraction** — all
the PC1 / ISI / contamination-aware logic was bypassed without any
diagnostic indicating why.

After: binary-format aware auto-detecting reader, matching the pattern
already established by `process_estimatedrift`, `process_localise`,
`process_mergespikes`, and `collision_viewer.py`.  Legacy text format
still works as a fallback.  All cluster modelling layers now actually
run on modern sessions.

### Cluster-id filter (enables strip-good-re-detect workflow)

Before: every cluster present in `.clu` had its spikes subtracted,
including cluster 0 (artefact) and cluster 1 (MUA) per neurosuite
convention.  After a strip pass the residual had no spikes left at all
— making the "strip good clusters, re-detect what was hidden
underneath" workflow structurally impossible.

After: `process_subtractspikes` accepts an 8th colon-separated field in
each group's spec string specifying which clusters to subtract:

  - `-` or missing: strip every cluster EXCEPT 0 and 1 (default)
  - `all`:          strip every cluster including noise/MUA
  - `2,3,5`:        strip only the listed cluster ids
  - `@path`:        read one integer cluster id per line from file

Cluster models are still built for every cluster that appears in the
`.clu` file — `clean_obs_waveform` uses models of neighbouring
non-stripped clusters when cleaning overlap contamination from
stripped spikes' observation windows.  Only the outer "actually
subtract this spike" step is guarded.

`ndm_stripdat` exposes this via two new YAML parameters plus a per-
group sidecar mechanism:

  - `stripClusters` (YAML, default `"default"`): global value passed
    to every group that does NOT have its own sidecar file.  Same
    semantics as the spec's 8th field (without the `@path` form).

  - `<session>.strip.<g>` (per-group sidecar, optional): plain text
    file with one integer cluster id per line (blank lines and `#`
    comments allowed).  Takes priority over the global value.  This
    is the recommended place to maintain "curated good units" lists
    as they grow during iterative strip/re-detect rounds.

### Iterative workflow now supported

```
  1. Sort, curate.  Identify clusters you trust as real units.
  2. Write their ids into <session>.strip.<g> per group.
  3. Run ndm_stripdat      → <session>-spkclean.dat
  4. Run ndm_redetectspikes → second-pass .res/.spk
  5. Sort the newly-detected spikes, curate newcomers.
  6. Append confirmed newcomers to <session>.strip.<g> and
     re-run ndm_stripdat against the ORIGINAL .dat
     (delete <session>-spkclean.dat or set overwrite=true first).
  7. Repeat until re-detection finds no new real units.
```

### `find_clean_mask` vectorised (~18× faster)

The O(n log n) contamination detector had a Python-level loop over
every spike with two `searchsorted` calls each.  On a 200k-spike test,
measured wall time drops from 584 ms to 32 ms (18×) with byte-exact
output parity verified across n = 10, 200, 5000 synthetic
spike-time arrays.  Full-scale recording sessions with 1M+ spikes
benefit proportionally more.

### Other fixes

- `_fit_exp_1ch` was being called twice per channel in
  `ExponentialWaveformModel.__init__` — once to extract the fitted
  evaluation and a second time just to test whether `popt is None`
  for the log message.  `curve_fit` is expensive; capturing `popt`
  from the first call saves one nonlinear optimisation per channel
  per cluster (a noticeable win on sessions with many clusters).

- When `scipy` is absent, `_fit_exp_1ch` returned `None` silently,
  which caused `ExponentialWaveformModel` to produce all-zero
  templates — cluster-aware subtraction silently degraded to
  near-nothing.  The pipeline now emits a loud 7-line warning at
  startup when scipy is missing and burst-correction or Layer 2
  modelling was requested, so the degradation is visible.

### `ndm_stripdat` changes

- Removed stale `check_commands_installed xpathReader` — the script
  uses `yaml_read` exclusively; the XML reader isn't a dependency.

- Added `overwrite` parameter (default `false`) and an
  `outputs_exist` guard on `<session>-spkclean.dat`.  Stripping a
  multi-hour recording can take tens of minutes; the guard prevents
  accidental re-runs.

- Added `stripClusters` parameter + per-group `.strip.<g>` sidecar
  lookup (see above).

- Expanded header docstring to document the new parameters and the
  iterative strip/re-detect workflow.

### Subtraction mode (--subtraction-mode, subtractionMode)

New flag selects between two distinct subtraction strategies:

- **`model`** (previous default, still available): cluster-aware
  Layer 1 (per-cluster mean template + ISI burst correction) or
  Layer 2 (exponential PC1 shape model) with contamination-aware
  neighbour cleaning and drift compensation.  Preserves the noise
  baseline at subtracted spike windows — residual looks like
  surrounding noise rather than a hole in the trace.  Appropriate
  when the cleaned `.dat` will feed into LFP extraction or other
  analyses that depend on continuous baseline statistics.

- **`raw`** (new, default in ndm_stripdat): Layer 0 direct
  subtraction of the raw `.spk.N` waveform.  Near-exact cancellation
  at the spike window — residual at subtracted spikes is below 1%
  of original amplitude.  Skips all model fitting (no scipy, no PCA,
  no exponential curve_fit), contamination mask, and ISI
  computation — significant performance win.  Appropriate for the
  iterative strip-good-re-detect workflow because residual spike
  amplitude falls far below any reasonable detection threshold.

End-to-end test confirms: raw mode leaves 0.0% residual at
stripped A and B cluster timestamps (vs ~30% for Layer 2 model fits
on the same synthetic fixture).  Cluster-id filtering continues to
work identically in both modes — MUA and noise clusters are
preserved untouched.

`ndm_stripdat` defaults `subtractionMode` to `"raw"` because the
primary use case (feeding `ndm_redetectspikes`) requires the
residual to fall below detection threshold; LFP users who need
the model-mode baseline preservation can set `subtractionMode:
"model"` in the YAML.

### Not fixed in this patch

- `process_subtractspikes` is still Python.  A C++ port of the chunk
  processor plus a Python-side `process_fitspikemodels` sidecar
  writer (same pattern as `process_pca` / `process_refeaturize`)
  would fit the rest of the project's C++-on-the-hot-path
  convention.  Deferred to a separate larger change.  The raw-mode
  path introduced here is already the fast path (no scipy, no
  model fitting) so for the strip-good-re-detect workflow the
  Python overhead is no longer a real problem.

- In `model` mode, Layer 2 (exponential PC1) on clean synthetic
  spikes leaves ~30% residual because scipy `curve_fit` with the
  current auto-init lands in a suboptimal local minimum.  The new
  `raw` mode sidesteps this entirely for the re-detect workflow.
  If model-mode residuals matter (e.g. for LFP extraction), the
  fit quality could be improved with better init parameters, but
  this is orthogonal.

- Drift correction in `process_subtractspikes` falls back to a
  hardcoded 25 µm uniform channel spacing because `ndm_stripdat`
  never plumbs the real `sitePositions_um` through.  Only affects
  `model` mode (raw mode skips drift entirely).
  `process_estimatedrift` and `process_localise` already do this
  properly; `ndm_stripdat` should be brought into line.

- The PC1-score clamp percentile (`PC1_SCORE_CLIP_PERCENTILE = 0.5`)
  is aggressive — it clips to `[0.5th, 99.5th]` of training PC1
  scores.  Only affects `model` mode.  Should be exposed as a
  parameter.
