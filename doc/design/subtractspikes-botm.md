## ndmanager-plugins — process_subtractspikes fixes (2026-04-19, rev 3 2026-04-20)

Nine fixes/features for the spike-subtraction pipeline.  Since the
initial 2026-04-19 revision, adds auto-generation of per-group strip
sidecars from YAML Quality labels (no manual file maintenance between
curation rounds), and a new `botm` subtraction mode that subtracts
spikes using Bayes-Optimal Template Matching (Proepper 2015) — ~100×
better residuals than `model` mode on the synthetic test fixture
while preserving the noise baseline.

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

### Auto-generated strip sidecars from YAML Quality labels

After manual curation in Klusters, each sorted unit gets an entry in
the session YAML's top-level `units:` block with (among others) a
`quality` free-text field.  `ndm_stripdat` now reads that field and
auto-generates the per-group strip list when no explicit
`<session>.strip.<g>` sidecar is present — so the iterative
curate → strip → re-detect loop requires no manual file
maintenance between rounds.

**Mechanism.**  For each electrode group `g`:

1. If `<session>.strip.<g>` exists (explicit operator override), it
   wins and is passed verbatim to `process_subtractspikes` as
   `@path`.  Unchanged behaviour.
2. Else, if `autoStrip=true` (default) and the YAML `units:` block
   has at least one entry for group `g`, the list is built from
   entries whose `quality` field matches (case-insensitively, after
   whitespace trimming) any of the configured `qualityTags`.
   Written to `<session>.autostrip.<g>` with a provenance header
   (generation date + source YAML + tags used) so the operator can
   inspect what fed the current run.  Regenerated every run so
   the list always reflects current YAML curation.
3. Else, the global `stripClusters` parameter is used (previously
   existing behaviour).
4. Else, the built-in default (strip every non-0/1 cluster).

**Two new YAML parameters** in the `ndm_stripdat` block:

  - `autoStrip: "true"|"false"` (default `"true"`).  Master switch.
    When off, only explicit sidecars and `stripClusters` are
    consulted — no auto-generation.
  - `qualityTags: "good,great,excellent"` (default).  Which quality
    labels count as "strip-worthy".  Labs using a different
    vocabulary (e.g. `"SU,single-unit,accepted"`) configure here.

**Three-state exit semantics** distinguish the cases cleanly:
auto-generator exits 0 with output if any units matched, exits 0
with empty output if the group was curated but no units matched
the tags (explicit "strip nothing for this group"), and exits 1
with empty output if the group has no YAML curation at all (caller
falls back to `stripClusters`).

The Klusters `quality` field is implemented as a free-text
`QLineEdit` so labs can and do use varying capitalisation and
surrounding whitespace.  Both are normalised before matching.

Seven unit tests confirm the expected behaviour (mixed
curation → correct subset; all-bad curation → empty list; no
curation → fallback signal; custom tag sets; case-insensitive match;
whitespace-trimmed labels; missing YAML → fallback).

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

### Not fixed in this patch → ### BOTM — Bayes-Optimal Template Matching (--subtraction-mode botm)

Third subtraction mode, implementing BOTM from Proepper 2015, §3.4.
Closes the "implicit white-noise" gap in `model` mode:

**Generative model**: `x_t = Σ_i Σ_τ s^i_{t-τ}·ξ^i_τ + η_t` with
`η ~ N(0, C)`, C the `(Tf·NC)×(Tf·NC)` noise covariance.

**Build steps** (per group, once):

1. **Noise covariance estimation** — `estimate_noise_covariance()` walks
   between-spike periods of the input .dat.  For each period of length
   ≥ `2 · nSamples`, take non-overlapping `nSamples`-wide windows,
   compute local covariance.  Weight-average by period length:

   ```
   C = Σ_period (len_period · C_period) / Σ_period len_period
   ```

   This is Proepper's explicit remedy for the "stitching pitfall"
   where simply concatenating noise periods and computing a single
   covariance under-estimates C because samples at period borders,
   originally far apart, get treated as adjacent.

2. **Ridge regularization**: `C += ridge · trace(C)/dim · I` before
   inversion.  Default ridge = 1e-3.  Raise if inversion fails on
   short recordings (exposed as `--botm-ridge` / `botmRidge`).

3. **Per-cluster matched filter**: `f^i = C^(-1) · ξ^i` and denominator
   `ξ^i^T · C^(-1) · ξ^i` cached in `BOTMClusterModel` at build time.
   Template ξ^i is the cluster mean waveform from the contamination-
   excluded subset (falls back to trimmed mean if clean subset is
   too small), same as Layer 1.

**Per-spike subtraction** uses Bayes-optimal amplitude:

```
a_i(x) = (x^T · f^i) / (ξ^i^T · f^i)
```

No clamp — the denominator already normalises template energy under
the whitened metric.  Subtract `a_i · ξ^i` from the buffer.  No drift
compensation and no burst correction in this version (deferred).

**Measured performance** on the synthetic 3-cluster test fixture:

```
Residual comparison on A-cluster (original peak = -398.7):
  raw mode:       0.0  (  0.0%)      zeros by construction
  model mode:    37.6  (  9.4%)      Layer 2 PC1 exp-fit
  botm mode:     -0.3  (  0.1%)      matched-filter amplitude
```

BOTM achieves ~100× better residual than `model` on this fixture,
while preserving the noise baseline outside spike windows (byte-
identical to input — raw mode also does this on this fixture but
in general raw cancels the whole .spk snippet including its noise
content).  Both `model` and `botm` are suitable for LFP extraction;
`botm` is strictly better for LFP unless the model's burst correction
or shape variability is needed.

**New CLI/YAML**:

- `--subtraction-mode botm` (alongside `model` / `raw`)
- `--botm-ridge 1e-3` (covariance inversion regulariser)
- YAML `subtractionMode: "botm"`
- YAML `botmRidge: "1e-3"` (read only when subtractionMode=botm)

**Deferred**:

- **SIC overlap handling** (Proepper §3.4.4).  The current BOTM mode
  processes each spike independently using its observation window.
  Proepper's Subtractive Interference Cancellation iterates: find the
  highest-discriminant spike, subtract, update remaining discriminants
  by the cross-term `ξ^i^T · C^(-1) · ξ^j_τ`.  For overlapping spikes
  of different clusters this improves amplitude estimation.  Not
  critical for the iterative workflow (the outer operator loop already
  serves as coarse-grained SIC at the curation level) but it would
  tighten within-group overlap handling if added later.

- **Drift compensation in BOTM**.  The current BOTM template has a
  fixed spatial reference frame.  Future work: pre-whiten the
  drift-shifted template, which requires shifting both ξ and
  reconstructing C^(-1)·ξ for each shift.  Expensive but tractable.

- **Pre-whitening for Layer 2 PCA** (Option A from the original
  modelling-recommendations document).  Superseded by BOTM for the
  main use case; still worth adding to improve Layer 2 if `model`
  mode remains in use for any pipeline.

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
