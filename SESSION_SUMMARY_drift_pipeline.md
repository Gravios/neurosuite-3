# Drift-Estimation Pipeline — Session Summary

**Date range:** May 2026 working session
**Reference data:** `jg05-20120316` (Buzsaki64L) + A32 linear sessions
**Commits landed (oldest → newest):**

```
dddb80e  ndm_estimatedrift: bring up to current standards
96fda50  ndm_driftcorrect:  geometric refeaturization to reference position
60eec7d  ndm_estimatedrift, ndm_driftcorrect: sharpness weighting + memmap + parallel
bb4bc1b  ndm_estimatedrift: geometry weight mode for V-shaped/linear probes
```

---

## 1. Architectural decisions made (and why)

### 1.1 Drift correction as refeaturization, not chunked CEM

Initial diagnosis: the existing `ndm_applydrift` → `SESSION.chunks.N` path adapts the *clustering* to drift but never actually corrects the data. Within-chunk drift inflates cluster covariances; between-chunk transitions are discontinuous; once KlustaKwik produces models there's no path back.

Decision: build `ndm_driftcorrect` that consumes `SESSION.dat.drift.P` and emits `.spkC(D).N` — drift-corrected waveforms reinterpolated to a reference probe position. The chunked-CEM machinery stays as a fallback for sessions with drift large enough that refeaturization alone can't cope, but it's no longer the default.

The half-built `shiftprobe_{cuda,hip,sycl}` kernels (currently disabled via `shiftprobe_disabled.cpp`) were the precursor for this — they did the same operation in the wrong place (inside KlustaKwik's CEM loop on already-projected feature vectors). The refeaturization plugin moves that operation up the pipeline, runs it once on raw waveforms, and persists the result.

### 1.2 `ndm_driftcorrect` is `.clu`-blind by design

The correction is purely geometric. Whether a spike was labelled cluster 0 (artifact), cluster 1 (MUA), or assigned to a real unit makes no difference: the recorded waveform is the same physical signal, and so is the corrected waveform.

This decoupling is what makes the iterative refinement loop work. A shank whose `.clu` contains nothing but 0/1 in iteration N can recover real units in iteration N+1 after refeaturization with the **propagated probe-level drift signal** from a curated sibling shank. Refeaturization doesn't gate on the (provisional) cluster labels.

### 1.3 Drift signal is absolute, not incremental

Each iteration of the refinement loop writes a new `SESSION.dat.drift.P` representing the *cumulative* offset between probe and reference. `ndm_driftcorrect` always reads the original `.spk(D)` and writes `.spkC(D)` from scratch. Successive iterations don't compound interpolation error.

### 1.4 Per-unit weighting evolved through three generations

Three weight modes are now selectable; only the default has changed.

**`count` (legacy):** `weight = sqrt(n_ref × n_win)`. Pure spike-count. Fails on shanks with mixed-amplitude clusters because a 50 Hz hash-only cluster outweighs a 1 Hz well-isolated unit.

**`sharpness` (intermediate default):** `weight = peak_curvature × sqrt(n_active_ref × n_active_win) × sqrt(n_spikes_ref × n_spikes_win)`. Adds two confidence terms: how peaked the xcorr is (parabolic-fit second-difference at the peak on L2-normalized profiles, range ≈ [0,1]), and how many sites carry meaningful signal (PTP > 10% of peak). Helps with mixed-amplitude shanks but doesn't distinguish near-field from far-field units.

**`geometry` (current default):** sharpness × `profile_drift_sensitivity`. The sensitivity term — L2 norm of the spatial derivative of the L2-normalized reference-window profile, computed on active sites only — separates "sharp xcorr peak because profile genuinely tracks drift" from "sharp xcorr peak because profile is invariant to drift." This was Gravio's V-electrode observation: far-field units have smeared profiles whose shape doesn't change with drift, so their sharp xcorr peaks at zero are uninformative regardless of how many spikes contribute.

Decisive demonstration: 3 units, shifts (+3, +9, +9), counts (200, 2000, 2000), realistic A32 sensitivities (0.020, 0.002, 0.002):
- count → median +9 µm (wrong)
- sharpness → median +9 µm (still wrong — curvature dwarfed by 10× spike count)
- geometry → median +3 µm (right — sensitivity factor knocks far-field down by 10×)

### 1.5 Probe-family-specific calibration

| Probe | Span | Pitch | Sensitivity range | Notes |
|---|---|---|---|---|
| Buzsaki64L | 140 µm/shank | 20 µm | ~5× | Geometry mode useful, not transformative |
| A32 linear | 1550 µm | 50 µm | ~20–50× | Geometry mode significantly improves estimates |
| NPX-class dense | full probe | ~25 µm | ~uniform | Every unit is effectively near-field; sharpness mode equivalent |

The local-spacing-per-unit fix (originally proposed) was deliberately **not** implemented — both Gravio's probe families have uniform vertical site spacing within a shank, so the global-median spacing in `xcorr_shift` is exact, not approximate. Reserve that fix for genuinely non-uniform probe geometries if/when they arise.

### 1.6 Memmap + multiprocessing instead of C++/Cython

C++/CUDA port was considered and rejected. The actual cost breakdown for octrode-scale data is dominated by I/O, not compute (~30 seconds total for `ndm_estimatedrift` on a multi-shank session). Even at NPX scale the per-window work is small enough that GPU dispatch overhead would eat any gain in the drift estimator itself.

Cython was considered as a middle ground and also deferred. Trigger to revisit: profiling on real NPX data showing `correct_waveform_batch` dominates non-I/O time, OR `ndm_clusterpca` per-cluster dispatch overhead becoming visible.

What was shipped instead:
- **`np.memmap`** for `.spk(D).N` and `.dat.drift.P` readers in both plugins. At octrode scale this is cosmetic; at NPX scale (60+ GB `.spk` per probe) it's the difference between "works" and "OOM-kill."
- **`multiprocessing.Pool` (spawn context)** for shank-parallel and group-parallel dispatch. `--n-workers` flag, default 1 (sequential, deterministic stderr ordering).
- **Output ordering canonicalized** (probeId asc, shankIndex asc) so two runs at different `nWorkers` produce byte-identical `.drift` and `.spkC(D)` files. Verified via md5sum.

Important caveat: parallel mode is *slower* than sequential at octrode scale because spawn overhead dominates the per-shank work. Only useful at NPX-class scale. Default 1 is correct for `jg05-20120316`.

---

## 2. The pipeline architecture as it stands

```
            ndm_extractspikes(_stderiv)  →  .spk(D).N
                          │
                          ▼
            ndm_pca(_stderiv)            →  .fet(D).N
                          │
                          ▼
            ndm_klustakwik               →  initial .clu.N
                          │
                          ▼
            ndm_estimatedrift            →  .drift YAML
                                         →  .dat.drift.P (per probe)
                          │
                          ▼
            ndm_driftcorrect             →  .spkC(D).N
                          │
                          ▼
            ndm_pca(_stderiv) [re-run]   →  .fetC(D).N
                          │
                          ▼
            ndm_klustakwik [re-run]      →  refined .clu.N
                          │
                          ▼
            ndm_estimatedrift [re-run]   →  sharper drift estimate
                          │
                  ┌───────┴────────┐
                  ▼                ▼
            converged?         not yet — iterate
                  │                │
                  ▼                └─ back to ndm_driftcorrect
            done
```

The iteration loop driver (`ndm_driftloop`) is not built yet; the docbook describes the manual symlink workflow until `pickInputPath` integration in `ndm_pca` / `ndm_pca_stderiv` / `ndm_klustakwik` lands.

---

## 3. What's deliberately not in the pipeline

### 3.1 Position-dependent (depth-varying) drift

Discussed and deferred. For Buzsaki64L (140 µm shank span) and A32 (1550 µm span) the rigid-body assumption is genuinely defensible — non-uniform mechanical drift is a Neuropixels-class problem (3.84 mm of contacts in distinct mechanical regimes). The architectural placeholder when this becomes needed: a `--drift-model rigid|linear|piecewise` flag, not per-cluster drift. Per-cluster drift conflates real differential motion with individual-unit waveform variability that has nothing to do with drift.

**Trigger to revisit:** `jg05-20120316` shows residuals after rigid-body correction that correlate with depth.

### 3.2 Per-cluster PCA refinement (`ndm_clusterpca`)

Discussed and deferred. The current `.fet` features come from PCA computed on all spikes in a group at once — good basis for initial separation, poor basis for *refining* established cluster boundaries. Per-cluster PCA after curation gives a basis where the first 2-3 components capture most within-cluster variability and other-cluster spikes project to large residuals — sharper Mahalanobis test than the global basis.

Sketched plugin: reads `.clu.N` and `.spk(D).N`, fits per-cluster PCA basis on cluster ≥ 2 only, projects every spike onto every basis, writes per-cluster Mahalanobis distances + top-K residuals as `.cluproj.N`. Klusters can then offer "show spikes within Mahalanobis distance D of cluster k" as a curation primitive.

Cross-pollinates with drift estimation: PC1 spatial profile is a richer fingerprint than PTP for low-SNR units. Could add `--profile-method ptp|cluster_pc1` to `ndm_estimatedrift` once `ndm_clusterpca` exists.

### 3.3 Local-spacing-per-unit correction in `xcorr_shift`

Considered. Unnecessary for Gravio's current probe families (uniform vertical spacing). The current code:
```python
spacing = float(np.median(np.diff(np.sort(valid_depths))))
return lag * spacing
```
is exact for uniform-pitch probes. Approximate only when sites are non-uniformly distributed in depth (some custom V-tip arrays, certain Neuropixels variants).

### 3.4 Kilosort-style `.dat`-shifting

The current architecture re-interpolates already-extracted waveforms (`.spk(D)` → `.spkC(D)`). Kilosort's "datashift" instead shifts the raw `.dat` and re-detects. The trade-off: `.dat`-shifting is more correct in principle (re-detection finds spikes the original threshold missed), but it requires the full re-detection pass per iteration, which is expensive.

For now: `.spk(D)`-level refeaturization is fine. If we ever want raw-data shifting, the right place is a one-off `ndm_shiftdat` plugin that applies the converged drift signal once at the end of the loop, producing `.datC` for downstream tools that don't speak the drift signal natively.

---

## 4. Open work, ordered by likely sequence

1. **`pickInputPath` chain** in `ndm_pca` / `ndm_pca_stderiv` / `ndm_klustakwik`. Smallest scope, unblocks clean iteration without symlink shuffles. The chain should be: `.fetC` → `.fetCD` → `.fet` → `.fetD` (and analogously for `.spk*`), with a `-DriftCorrected` flag to override.

2. **`ndm_driftloop`** — bash driver that runs estimate → correct → re-PCA → recluster → re-estimate until `max|Δd(t)| < ε`. Builds trivially on top of the existing primitives once `pickInputPath` lands. Iteration cap recommended at 5.

3. **Klusters `.dat.drift.P` sub-bar visualization**. Display the drift trace as a thin sub-bar above the time bar in trace and cluster views. No mutation, just visualization. Lets the curator see at a glance whether the iteration converged.

4. **`ndm_clusterpca`** — per-cluster PCA + projection + Mahalanobis distances. Independent of drift. Useful on its own for sharpening curation in Klusters.

5. **Profile-method selection in `ndm_estimatedrift`** — once `ndm_clusterpca` exists, add `--profile-method ptp|cluster_pc1` so PC1-based spatial profiles can replace PTP for low-SNR units.

6. **Position-dependent drift model**, only if real-data residuals warrant it.

---

## 5. Calibration parameters that may need tuning on real data

These were chosen on synthetic data and should be validated against `jg05-20120316` and the A32 sessions:

- **Active-site threshold**: 10% of per-window peak amplitude (`per_unit_xcorr_shift` and `profile_drift_sensitivity`). Too low → far-field tail noise inflates `n_active`. Too high → near-field units near the array edge get fewer active sites than they should.
- **Outlier threshold**: 5.0 µm pairwise CoM distance change. Originally chosen for octrode (140 µm span); may need to be raised for A32 where genuine local variability between distant units could exceed this.
- **`passthroughThreshUm`**: 0.25 µm in `ndm_driftcorrect`. Below this, byte-copy passthrough is used instead of interpolation. Calibrated for int16 µm signal precision; should match the noise floor of `ndm_estimatedrift`.
- **MergeThresh** (`KlustaKwik`): chi²(nSpatialDims, 0.99). Already auto-calibrated, but worth re-confirming after refeaturization changes the feature distribution.

---

## 6. Reproducibility notes

- All three weight modes (`geometry`, `sharpness`, `count`) remain selectable. Pin `weightMode: count` in YAML to reproduce pre-r2 estimates bit-for-bit; pin `weightMode: sharpness` for r2-era estimates; default `geometry` for current.
- Sequential vs parallel produces byte-identical output files (canonical sort, deterministic interpolation, no inter-shank state).
- The `.dat.drift.P` binary signal is regenerated unconditionally each run when `.dat` is present. Currently has no consumer at HEAD beyond `ndm_driftcorrect`.
- `process_estimatedrift.py` runs source-blind by default — every shank with curation produces an estimate. Use `--source-group N` to make one curated shank drive estimates for all sibling shanks on the same probe (the probe-level rigid-body assumption).

---

## 7. Tooling pieces in place

- `ndm_estimatedrift`, `process_estimatedrift.py`, `ndm_estimatedrift.docbook` — complete, with three weight modes, memmap reader, shank-parallel dispatch.
- `ndm_driftcorrect`, `process_driftcorrect.py`, `ndm_driftcorrect.docbook` — complete, with memmap reader, group-parallel dispatch.
- `echo_message` shim added to `ndm_functions` (silently un-broke `ndm_decomposecollisions`, `ndm_setupgroups`, `ndm_applydrift`, `ndm_stripdat`, `ndm_estimatedrift`).
- `CMakeLists.txt` install entries and manpage registration for both plugins.

---

## 8. Pieces that exist but need documentation/wiring updates

- The Python helpers contain `--source-group`, `--outlier-threshold`, and other flags not yet exposed via the bash wrapper's `read_script_parameter` path. Easy wins next time someone touches these scripts.
- `ndm_driftcorrect.docbook` describes a manual symlink iteration workflow that should become unnecessary once `pickInputPath` lands; remove or downgrade that section once it does.

---

## 9. Failure modes seen during the session and how they were resolved

| Symptom | Root cause | Fix |
|---|---|---|
| `ndm_estimatedrift: line 60: echo_message: command not found` | Function used by 5 plugins but never defined in `ndm_functions` | Added `echo_message()` shim delegating to same body as `echo_info` |
| `UnboundLocalError: cannot access local variable 'n_sites'` | `if is_stderiv: n_sites -= 1` referenced before assignment, AND wrong: `.spkD.N` stores full nCG channels (channel-drop happens at `.fetD` time) | Removed the buggy block entirely |
| YAML output schema didn't match Python output | Docbook predated the per-unit-xcorr rewrite, still showed CoM-only schema | Docbook rewritten to match current `methods` / `primaryMethod` / per-unit-xcorr block format |
| Synthetic test where geometry mode looked weaker than expected | Synthetic noise model didn't perfectly track truth — near-field unit's xcorr undershot at small drifts | Replaced synthetic with direct `weighted_median` test using realistic A32 sensitivity values; clean +9 → +3 flip |

---

## 10. Reference data conventions

These are the file/format conventions established or confirmed during this session:

- **`.dat.drift.P`**: int16 µm, one sample per recording sample, P is the 1-based probe index in the YAML `probes` list. Generated by `ndm_estimatedrift`. Currently consumed only by `ndm_driftcorrect` (and the disabled `shiftprobe` kernels).
- **`.spkC.N` / `.spkCD.N`**: drift-corrected waveforms. Same on-disk layout as `.spk.N` / `.spkD.N` (int16 sample-major). Suffix matches input variant: `.spkD.N` → `.spkCD.N`, `.spk.N` → `.spkC.N`.
- **`.drift` YAML format `1.0`**: methods `[per_unit_xcorr, xcorr_population]`, `primaryMethod: per_unit_xcorr`, with per-window detail blocks containing per-unit weight breakdowns. New field `weightMode: geometry|sharpness|count` records which mode produced the file.
- **`.spkD.N` channel count**: stores the **full nCG channels**. Stderiv transform is in-place; no channel is dropped. Channel-dropping happens at `.fetD` time in `process_pca_stderiv`.

These are now documented in `STANDARDIZATION.md` (§3.3) and the relevant docbook files.
