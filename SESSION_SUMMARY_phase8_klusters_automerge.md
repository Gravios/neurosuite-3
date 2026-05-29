# Phase 8 + Klusters Auto-Merge — Session Summary

**Date:** 2026-05-29
**Reference data:** `sirotaA-jg-000005-20120312` group 6, stderiv pipeline, 64-core box
**Commits landed (oldest → newest):**

```
e8a2c1b  klusters: split General preferences tab into five grouped tabs (+ icons)   [0067]
b1d4f7e  klusters/Auto-Merge: populate settings tab + Configuration round-trip      [0068]
484b896  klusters/Auto-Merge: add action + algorithm (template xcorr + union-find)  [0069]
54a2ac8  klustakwikExp: Phase 8 variance-targeted knn-split (diffuse-cluster cleanup) [0070]
```

(Hashes are the in-tree IDs at the time this doc was written. `git log --oneline | head` for the current truth.)

---

## 1. What landed this session

Two parallel work streams stacked into a coherent set:

**KKE-side (klustakwikExp/):** new Phase 8 — a variance-targeted knn-split pass that runs iterated Phase 4b's WaveKnn-split machinery on the high-variance clusters only, with FullCEM intentionally skipped. Fills the gap left by Phase 4c being the wrong tool for diffuse-without-modes clusters.

**Klusters-side (klusters/):** new Auto-Merge action that uses the same template cross-correlation mechanism KKE uses offline, so interactive curation in klusters and the offline KKE merge produce consistent decisions on the same data. Required first refactoring the General preferences tab into five focused tabs (one of which holds the new Auto-Merge settings), which has independent UX value.

Verification at session end:
- All four patches stack clean from `main` via `git am --3way`.
- KKE: `c++ -std=c++20 -fsyntax-only -fopenmp -Wall -Wextra ...` builds with zero warnings.
- KKE: live run on user's session converged Phase 8 cleanly with the recommended flags; output matched the design ("It worked well" — user, post-run).
- Klusters: full Qt + CMake build not done in patch-author environment; tested at user end.

---

## 2. Background — what these patches address

### 2.1 Phase 4c was stuck on FullCEM stragglers

On the user's session, the run captured at `Phase 4` digest showed 200 iters with merges stuck near 0 while splits won every iteration (1318 → 1968 clusters). Phase 4c then took those 1968 clusters and started its FullCEM-source pass — and stuck. Diagnosis (this session):

- Phase 4c parallelizes over **chunks** (~30 of 36 had work after disjointness drops). With 64 cores, ~34 were idle by construction.
- Within each chunk, sources were processed serially. Sources are *pooled super-clusters* (source + 2 neighbors), so their cost is roughly 3× the source size — superlinear in FullCEM's covariance ops.
- A few chunks with fat pools dominated wall-time while everyone else was done. Looked like a hang; was a work-imbalance straggler.

The deeper problem: **FullCEM is the wrong tool for the user's data**. The user described the suspect clusters as "diffuse with no obvious modes." FullCEM tries to find Gaussian-mixture sub-structure; in a diffuse smear there is no mixture, so FullCEM either no-ops or invents spurious modes from noise. Phase 4c's sampling-based approach (3 random sources per chunk per iter) then spreads FullCEM cycles across pointless work.

### 2.2 The QualityWeightedSplit dispatcher routes 0 → CEM on clean recordings

Surfaced this session as a diagnostic finding (no fix yet — see §7). The dispatcher routes each candidate cluster to CEM (high ISI contamination) or WaveKnn (high waveform variance) by comparing `max(contamN, wavVarN)` after min-max normalisation across the pool. On a clean recording with near-zero refractory violations across all clusters, `contam` collapses to all-zeros → after min-max the contamN range is 0 → the single cluster that would have routed to CEM (the one with min wavVar) gets dropped by the top-N filter. Result: 0 → CEM, structurally, every iteration.

For the user's diffuse-cluster data this is *correct* (no contam signal → CEM has nothing to do), but it's worth being explicit about: the dispatcher independently arrived at the same conclusion that motivated Phase 8.

### 2.3 No interactive merge in klusters used the same algorithm as KKE

Klusters has had `Group Clusters` (Ctrl+G) for manual merging since the original Hazan codebase — it groups whatever the user has selected, no scoring. The user wanted an action that *applies the same template-xcorr scoring KKE uses for `WithinChunkTemplateMatch` / `WithinChunkTemplateMatchMedianKnn`* — same defaults, same shape — so interactive merges and offline merges produce consistent decisions on the same data.

---

## 3. Phase 8 — variance-targeted knn-split (KKE)

### 3.1 Mechanism

Per iteration, up to `Phase8VarianceSplitMaxIters` times:

1. **Per chunk in parallel**, compute `ρ = V_res / P_sig` on signal-support channels for each cluster (definition in §3.2).
2. Clusters with `ρ ≥ Phase8VarianceThreshold` and size `≥ minSize` form the WaveKnn-split allowlist.
3. `WaveKnnSplitPerChunk(allowlist)` — redistributes each high-ρ cluster's spikes to nearby reference clusters by k-NN voting. Spikes that don't vote strongly stay as a (smaller) residual in the source.
4. `WithinChunkTemplateMatch[MedianKnn]` for merge cleanup.
5. Quit if no spikes relabeled AND no merges (converged), or after `maxIters`.

### 3.2 The ρ metric

Same definition Phase 4c uses, extracted into the shared method `KK::computeClusterTightnessRho`:

- For each cluster's spikes, read waveforms via `TimeShiftReadSpikeWave` (requires `MaxTimeShift > 0`).
- Channel-major mean template `mean[ch*nSamp + s]`.
- Per-channel energy `Ech[ch] = Σ_s mean[ch,s]²`. Signal channels are those with `Ech ≥ τ · max(Ech)`, where `τ = Phase8VarianceSignalChannelFraction` (default 0.1).
- `P_sig = Σ_{signal ch, s} mean[ch,s]² / (nSig · nSamp)` — per-element signal power on the signal support.
- `V_res = Σ_{spike, signal ch, s} (w − mean)² / (ok · nSig · nSamp)` — per-element residual variance on the signal support.
- `ρ = V_res / P_sig`. Scale-free (amplitude cancels); per-element averaging on signal support makes concentrated and diffuse footprints directly comparable.
- Returns `+∞` when ρ can't be measured (too few spikes, no signal support, zero denominator). Phase 4c uses +∞ to mean "untouchable, leave alone" (masks below threshold). Phase 8 uses +∞ the same way (targets above threshold, but +∞ is special-cased out via `std::isfinite` so degenerate clusters aren't selected).

**Phase 4c and Phase 8 use the same metric with inverted eligibility.** Phase 4c masks `ρ < Phase4cTightnessThreshold` (tight clusters get left alone); Phase 8 targets `ρ ≥ Phase8VarianceThreshold` (loose clusters get worked on). This is a clean design: one metric, two cutoffs, complementary phases.

### 3.3 Why FullCEM is skipped

The target case (diffuse smears, no modes) is the exact failure mode for FullCEM. Running it would either no-op (the EM converges back to the same diffuse cluster) or invent spurious modes from noise. WaveKnn-split is the right tool because it doesn't require internal structure — it asks each spike "is there a better-fitting neighbor cluster?" and reassigns when yes. Over iterations the diffuse cluster shrinks as its peripheral spikes leak to better-fitting neighbors, leaving either a smaller well-fitting core or nothing.

This is the inverse of Phase 4c's behavior, where FullCEM is mandatory and split sources get re-pooled with neighbors for the EM step. Phase 4c's design assumed under-split clusters with real Gaussian-mixture structure that pooling would help find. Phase 8's design assumes diffuse clusters that need redistribution, not re-partitioning.

### 3.4 CLI flags (all five new)

```
-Phase8VarianceSplitEnable            0    # master switch
-Phase8VarianceSplitMaxIters          3
-Phase8VarianceThreshold              0.10 # ρ ≥ this is eligible
-Phase8VarianceSignalChannelFraction  0.1  # τ; same default as Phase 4c
-Phase8VarianceMinClusterSize         0    # 0 = auto = max(nFullDims+5, 25)
```

Default off preserves prior behavior. On the user's session with these defaults, Phase 8 converged in 2-3 iterations.

### 3.5 Hook position

After Phase 4c at both `RunChunkedCEM` call sites in `KK.cpp` (~4615 and ~6043). Phase ordering is now:

```
Phase 4 (CEM loop, with 4b split + within-chunk merge)
Phase 4c (neighborhood remix, if enabled)        — sampled, can be slow on big sessions
Phase 8 (variance-targeted knn, if enabled)      — new; targets diffuse clusters
Phase 5 (cross-chunk consolidation)
Phase 6 / 7 (downstream)
```

### 3.6 Expected log output

```
[Phase 8] Variance-targeted knn-split: up to 3 iters, ρ_thresh=0.100, min size=25 (FullCEM skipped — diffuse clusters have no modes)
[Phase 8] iter 1/3: 12 high-variance sources, 4823 spikes redistributed, 2 merge(s)
[Phase 8] iter 2/3: 7 high-variance sources, 1106 spikes redistributed, 0 merge(s)
[Phase 8] iter 3/3: 2 high-variance sources, 84 spikes redistributed, 0 merge(s)
[Phase 8] done: 21 source-iterations, 6013 spikes redistributed
```

Source count should drop each iteration as previously-diffuse clusters shrink below threshold or dissolve. **If the banner never appears**, Phase 4c is still running upstream (the most common cause this session) — check Phase 4c's progress in the log, or disable Phase 4c (`-Phase4cRemixEnable 0`) to give Phase 8 a clean run.

### 3.7 Tuning advice

- **`Phase8VarianceThreshold` too low → too aggressive.** Many clusters above the bar each iteration; spikes get redistributed across the whole post-Phase-4 cluster set. Raise to 0.15 or 0.20 if you see this.
- **Threshold too high → undertreatment.** Few clusters cross the bar, diffuse smears survive. Lower to 0.05 if specific suspect clusters aren't being touched.
- **Source count not dropping across iterations → not converging.** Either threshold is forcing newly-redistributed clusters back above the bar each pass (cap `maxIters` to 1 or 2 to break the loop), or your data is genuinely irreducibly diffuse (in which case Phase 8 has done what it can — accept and move on).

---

## 4. Klusters preferences refactor (patch 0067)

### 4.1 What changed

The single General tab (8 group boxes: crash recovery, undo, KlustaKwik reclustering, realign, DipSplit, KNN-split, display settings, template-matrix display) is now split into five focused tabs:

| Tab | Group boxes from old General | Widget count |
|---|---|---|
| Display | background color, marker size, line width, autoscale margin, white printing, auto-show matrices, template-matrix display thresholds | 8 |
| Session | crash recovery, undo | 3 |
| Reclustering | KlustaKwik executable + args, auto-select features, auto-select N features, mean-subtracted subdim | 6 |
| Refinement | realign (threshold/iters/maxshift), DipSplit (minsize/bloat/valley), KNN-split (K/threshold/minNew/minRef) | 10 |
| Auto-Merge | (new — see §5) | — |

All 27 controls from the original General tab accounted for. **Widget names unchanged** — QSettings save/restore keys are identical, so existing user preferences are preserved across the refactor.

Each new tab follows the existing `PrefClusterView` / `PrefWaveformView` pattern:

- `prefXxxlayout.ui` — Qt Designer file with the widgets.
- `prefXxxlayout.{h,cpp}` — trivial wrapper class `PrefXxxLayout : public QWidget, public Ui_PrefXxxLayout`.
- `prefXxx.{h,cpp}` — `PrefXxx : public PrefXxxLayout` with getters/setters + any slot glue (e.g., `PrefSession::updateCrashRecoveryTimeInterval`, `PrefReclustering::updateReclusteringExecutable`).

### 4.2 Icons

Five new 32×32 RGB PNGs in `src/klusters/src/icons/`, matching the existing `clusterview.png` / `waveformview.png` style (black background, simple symbolic pixel-art with saturated primaries):

- `display.png` — 2×2 color palette
- `session.png` — cyan circular arrow (undo / recovery)
- `reclustering.png` — three colored dots with cyan grouping arc
- `refinement.png` — silver/grey gear
- `automerge.png` — red + blue blobs merging with white inward arrows

Generated with PIL (script preserved in the patch commit message for reproducibility). All five registered in `klusters-icons.qrc`.

### 4.3 Files affected

```
DELETED  src/klusters/src/prefgeneral.{h,cpp}
DELETED  src/klusters/src/prefgenerallayout.{h,cpp,ui}
ADDED    src/klusters/src/prefdisplay{,layout}.{h,cpp,ui}
ADDED    src/klusters/src/prefsession{,layout}.{h,cpp,ui}
ADDED    src/klusters/src/prefreclustering{,layout}.{h,cpp,ui}
ADDED    src/klusters/src/prefrefinement{,layout}.{h,cpp,ui}
ADDED    src/klusters/src/prefautomerge{,layout}.{h,cpp,ui}
ADDED    src/klusters/src/icons/{display,session,reclustering,refinement,automerge}.png
MODIFIED src/klusters/src/prefdialog.{h,cpp}     -- five new PrefXxx* members; dispatch logic
MODIFIED src/klusters/src/klusters-icons.qrc
MODIFIED src/klusters/src/CMakeLists.txt
```

External callers: none. `PrefGeneral` was only referenced from `prefdialog.cpp` (verified with `grep -rnE 'prefGeneral|PrefGeneral'` before the refactor), so the rename surface is fully contained.

---

## 5. Klusters Auto-Merge action (patches 0068 + 0069)

### 5.1 Mechanism

Same template-cross-correlation algorithm KKE uses in `WithinChunkTemplateMatch` / `WithinChunkTemplateMatchMedianKnn`:

1. Filter candidate clusters (skip 0 = artefact, 1 = noise, anything below `minClusterSize`).
2. Per cluster, build a template — *mean* of all member waveforms, or *median* across up to `medianK` sampled waveforms. Median mode uses a fixed RNG seed (`0x4d525142`) so previews are reproducible across runs on the same data.
3. Optional Hann taper on each template (suppresses edge-discontinuity contributions to xcorr; matches KKE's `TemplateMatchTaperHannSamples`).
4. Pairwise normalised xcorr `score = max_lag |xcorr| / sqrt(|a|² · |b|²)` with bounded `maxShift` (defaults to `nSamp/4` when set to 0, matching KKE's `WithinChunkTemplateMatch`).
5. Union-find on score ≥ threshold pairs → connected components of size ≥ 2 are the merge groups.
6. If preview is enabled, modal dialog listing groups with a checkbox per group. OK applies checked subset; Cancel applies none.
7. Apply each accepted group via `doc->groupClusters(g.clusters, *view)` — the existing merge path. Integrates with klusters' undo/redo automatically.

### 5.2 Default settings match KKE flag defaults

Set deliberately so the interactive klusters merge and the offline KKE merge produce *consistent* decisions on the same data:

| Setting | Default | KKE equivalent |
|---|---|---|
| Algorithm | Median | matches `MedianKnnTemplateMatchEnable=1` |
| Median K | 50 | matches `MedianKnnTemplateMatchK` |
| Score threshold | 0.98 | matches `TemplateMatchScore` |
| Max shift | 0 (auto = `nSamp/4`) | matches `WithinChunkTemplateMatch` internal |
| Hann taper samples | 0 (off) | matches `TemplateMatchTaperHannSamples` |
| Min cluster size | 25 | matches KKE's clusters threshold |
| Target scope | Selected | safer default |
| Preview before apply | On | safer default |

### 5.3 How to use

- **Action menu**: Action → "Auto-Merge Similar Clusters..." (next to "Group Clusters")
- **Toolbar**: new "Auto-Merge" toolbar icon (red+blue merging blobs with white inward arrows) next to the existing "Group" icon
- **Shortcut**: Shift+G (G is reserved for the existing Group Clusters action)
- **Settings**: Preferences → Auto-Merge tab (configures algorithm, scope, threshold, etc. — see §4)

Workflow for *Selected* mode:
1. Select 2+ clusters in the palette.
2. Shift+G.
3. If any pair scores ≥ threshold, preview dialog appears listing groups.
4. Uncheck groups to skip them. OK applies the rest.

Workflow for *All Active* mode:
1. Open Preferences → Auto-Merge, set scope to "All active clusters", close.
2. Shift+G.
3. Progress dialog reads waveforms (synchronous, but `processEvents()` keeps UI responsive; Cancel works).
4. Preview dialog shows all proposed groups across the whole document. Inspect, uncheck unwanted, OK to apply.

### 5.4 Files affected

```
ADDED    src/klusters/src/autoMerge.{h,cpp}      -- algorithm + preview dialog
ADDED    src/klusters/src/icons/auto_merge_tool.png  -- 22x22 RGBA toolbar icon
MODIFIED src/klusters/src/klusters.{h,cpp}       -- mAutoMerge action + slotAutoMerge
MODIFIED src/klusters/src/prefautomerge.{h,cpp}  -- settings UI getters/setters
MODIFIED src/klusters/src/prefautomergelayout.ui -- replaces placeholder from 0067
MODIFIED src/klusters/src/configuration.{h,cpp}  -- 8 new fields + QSettings round-trip
MODIFIED src/klusters/src/prefdialog.cpp         -- dispatch updateDialog/Configuration/Default to PrefAutoMerge
MODIFIED src/klusters/src/klusters-icons.qrc
MODIFIED src/klusters/src/CMakeLists.txt
```

---

## 6. Recommended starting configuration

### 6.1 KKE — Phase 8 enabled, Phase 4c off

For the diffuse-cluster case (the user's data) — drop Phase 4c, use Phase 8:

```
-Phase4cRemixEnable                0
-Phase8VarianceSplitEnable         1
-Phase8VarianceSplitMaxIters       3
-Phase8VarianceThreshold           0.10
-Phase8VarianceSignalChannelFraction 0.1
-Phase8VarianceMinClusterSize      0
```

If a session does have refractory contamination (real multi-unit mixtures with refractory violations), Phase 4c still has value — keep `Phase4cRemixEnable=1` and run Phase 8 *after* it.

### 6.2 Klusters — Auto-Merge defaults

First-time use: leave the defaults, switch scope to "Selected" if not already, run on 5-10 selected clusters with preview on. Verify the proposed merges look reasonable. Then experiment with "All active" + lower threshold (0.95) to sweep the whole document.

---

## 7. Outstanding / promising ideas — next session work

Ordered by likely value-per-effort, with rough scope estimates and design rationale. Items 7.1 and 7.2 are the highest-priority follow-ups.

### 7.1 Phase 4b informative logging (compact)

**Status:** scoped this session, implementation deferred. User selected: "all three" (timing + per-source detail + dispatch detail) at the **compact** verbosity level ("one extra summary line per Phase 4b call").

**Scope:** ~80-100 lines added across `KK.cpp:11417` (QualityWeightedSplit summary), `~12195` (FullCem header), `~12481` (FullCem summary), `~17415` (WaveKnn summary). Add `contam` / `wavVar` raw-range printout so the "0 → CEM" diagnosis we did this session is self-evident from the log. Add per-source FullCem ms breakdown — surfaces the fat-pool stragglers without needing -Verbose 2.

**Why it matters:** the "Phase 4c stuck on FullCem" diagnosis we did manually this turn would have been instant with the timing breakdown. The "0 → CEM dispatcher" diagnosis required reading source code; with raw-range output it would be a one-line read from the log.

### 7.2 Phase 4c parallelism flatten — `(chunk, source)` joint parallel-for

**Status:** scoped this session. Design clear, implementation deferred.

**Problem:** Phase 4c parallelizes over chunks (~30 of 36 active after disjointness drops), so on a 64-core box ~34 cores idle by construction. Within each chunk, the per-chunk allowlist of sources is processed serially. A single chunk with a fat pool blocks completion.

**Scope:** ~80 lines. Build a flat `vector<(ck, lc, pool)>` list from the allowlist map, sort by descending pool size for longest-processing-time scheduling, parallelize over the flat list with `#pragma omp parallel for schedule(dynamic, 1)`. Per-pool synchronisation needed because pools from different chunks already touch different `perChunkClass`/`perChunkModels` slots — the existing per-chunk model refresh is already source-local within Phase 4c, so it should generalize cleanly. The only shared state is the result accumulators which need atomic adds or per-thread accumulation + final reduce.

**Why it matters:** unblocks 64-core utilization on Phase 4c when sources are few-per-chunk but many in aggregate (the exact pattern that bit us this session). Pairs naturally with 7.1 — once you can see per-source timing, you'll want to make the slow sources parallelize.

**Note:** even with this fix, **Phase 4c remains wrong for diffuse-cluster data** (no FullCEM modes to find). Fix is for the contaminated-session case where Phase 4c is the right tool but the implementation is slow.

### 7.3 `tmNormXcorr` extraction to shared header

**Status:** documented as a TODO in `autoMerge.cpp` patch 0069 commit message.

**Problem:** `normXcorr` is duplicated verbatim across `src/klusters/src/templatematrixthread.cpp` and `src/klusters/src/autoMerge.cpp`. Any algorithmic improvement (better lag handling, vectorization, anti-aliasing) has to be done twice or risk divergence.

**Scope:** ~30 minutes. Extract to `src/klusters/src/templatexcorr.h` (or `src/libklustersshared/`, depending on whether other code needs it). Both files include and call. No semantic change.

### 7.4 AutoMerge async QThread variant

**Status:** documented as a follow-up in `autoMerge.cpp` patch 0069.

**Problem:** Current `AutoMerge::computeProposals` runs synchronously on the GUI thread with `QApplication::processEvents()` between clusters. For Selected mode (handful of clusters) this is fine; for All Active mode on a session with hundreds of clusters and large spike counts, the waveform-read phase blocks the UI for seconds-to-tens-of-seconds. Cancel works but the progress dialog can look frozen.

**Scope:** ~150 lines. Wrap `computeProposals` in a `QThread` worker mirroring `TemplateMatrixThread` (already in the codebase, can crib pattern). Post progress + result via signals. UI stays fully responsive. Preview dialog appears on completion. Same caveat about clusterPalette selection — capture at start, don't read at end.

**Why it matters:** without this, "All Active" mode on big sessions is uncomfortable to use. Selected mode is fine without.

### 7.5 AutoMerge median template — feature-space K-nearest

**Status:** documented as a known divergence from KKE's MedianKnn.

**Problem:** KKE's `MedianKnnTemplateMatch` selects the K spikes nearest the cluster centroid in feature space — robust against outlier spikes that would drag the median. AutoMerge currently subsamples K spikes uniformly at random with a fixed seed.

**Scope:** ~50 lines. Compute cluster centroid in feature space, distance-rank spikes, take top K, build median from those. Needs the cluster's feature vectors and centroid — already accessible via `Data::featuresOfCluster` (or equivalent). Replaces `std::shuffle` + `assign(idx, idx+K)` in `autoMerge.cpp:170`.

**Why it matters:** marginal for most use; matters for clusters with bursting / contamination where a few outlier spikes can shift the random-K median. The xcorr is robust enough that the difference is usually < 1% on the score, but on borderline-threshold pairs (e.g., 0.975 ↔ 0.98) it could flip a merge decision.

### 7.6 `QualityWeightedSplitForceCemMin` flag — CEM-route escape hatch

**Status:** mentioned in §2.2 diagnosis. Defensive flag for the cleanly-recorded session case where dispatcher starves CEM.

**Problem:** if the user has a session with low refractory contamination across all clusters but real Gaussian-mixture structure that should be FullCem-split, the dispatcher will route 0 → CEM and the substructure stays unsplit.

**Scope:** ~10 lines. Add `INT_PARAM(QualityWeightedSplitForceCemMin)` default 0. In the dispatcher, after the routing loop: if `nCem < forceMin`, promote the top-`(forceMin - nCem)` pool members (by `wavVar`, descending, excluding those already in CEM) into the CEM route.

**Why it matters:** the current dispatcher is structurally biased against CEM on clean recordings. Phase 8 covers the diffuse-cluster case; this flag covers the inverse (cleanly-recorded substructure case) without changing the dispatcher's normal behavior.

### 7.7 `QualityWeightedSplit` per-route quotas

**Status:** discussed this session as an alternative to 7.6.

**Problem:** the dispatcher's "rank by `max(contamN, wavVarN)`, take top N" is one routing strategy. An alternative: "take top N/2 by contam → CEM; take top N/2 by wavVar → knn; resolve overlaps." Different bias.

**Scope:** ~30 lines + a flag `-QualityWeightedSplitMode {balanced, max-rank}` to select.

**Why it matters:** lower priority than 7.6. If 7.6 works for the contaminated-session case, this is over-engineering.

### 7.8 Phase 8 global variant — post-Phase-5 consolidated clusters

**Status:** flagged in patch 0070 commit message.

**Problem:** Phase 8 runs per-chunk before Phase 5. The variance estimator uses each chunk's local view of a cluster. If a unit looks diffuse in chunk *k* but tight in chunk *k+1*, Phase 8 targets chunk-*k*'s instance while leaving chunk-*k+1*'s alone. For drift-dominated sessions this is correct (drift makes per-chunk processing the right scope). For sessions where post-Phase-5 consolidation reveals diffuse global clusters that no single chunk flagged, you'd want a global pass.

**Scope:** ~200 lines. New `RunPhase8GlobalVarianceSplit` operating on consolidated `Class[]` array after Phase 5. Uses the same ρ metric but over the cluster's full spike set, not per-chunk. WaveKnn-split equivalent operating on global cluster IDs (would need a non-trivial adaptation — current `WaveKnnSplitPerChunk` is hardwired to per-chunk arrays).

**Why it matters:** unclear without data showing post-Phase-5 diffuse global clusters that survived Phase 8's per-chunk pass. Don't build speculatively — investigate first.

### 7.9 Phase 4c refactor to use `computeClusterTightnessRho`

**Status:** patch 0070 left Phase 4c's inline lambda in place to avoid breaking working code.

**Problem:** Phase 4c has a ~50-line inline lambda computing ρ. Patch 0070 extracted the same logic into `KK::computeClusterTightnessRho` for Phase 8 to use. Phase 4c still has its own copy.

**Scope:** ~30 minutes. Replace the lambda body with a call to `computeClusterTightnessRho`. Need to thread `rbuf` from the OMP parallel scope correctly (same pattern Phase 8 uses).

**Why it matters:** small. Cleaning up the duplication makes the metric definition single-sourced — future changes propagate to both phases automatically.

### 7.10 Phase 8 ρ-threshold auto-tuning

**Status:** speculative; would need pilot data.

**Idea:** instead of fixed `Phase8VarianceThreshold=0.10`, derive threshold from the data — e.g., "target the top 5% of clusters by ρ each iteration." Adaptive thresholding handles datasets with different overall noise levels without retuning.

**Scope:** ~40 lines. After computing ρ for all clusters in a chunk, quantile-select the top-fraction. Replaces the `rho >= thr` comparison with `rho >= percentile(ρs, fraction)`.

**Why it matters:** quality-of-life. The fixed threshold works well on the user's data; whether it generalizes to other sessions without retuning is an open question that data would answer.

### 7.11 Phase 4 over-splitting — `KlustersRealignAfterPhase4 1` insufficient?

**Status:** observation, not yet investigated.

**Problem:** user's session this run had `KlustersRealignAfterPhase4=1` enabled and Phase 4 still hit the 200-iter cap with cluster count climbing (1318 → 1968). The hypothesis was that in-loop realign would stop alignment-jitter from masquerading as feature-space modes; that hypothesis isn't fully borne out.

**Scope:** investigation, not implementation. Hard data needed: per-iter realign stats (how many spikes were re-shifted? how much did the realign change them?). If realign is firing but the splits keep happening, the splits are real (not jitter) — accept and let Phase 5/6/7 consolidate. If realign is *not* firing (cache hits, or m_timeShiftReady false), that's a separate bug.

**Why it matters:** the 1968-cluster output going into Phase 5 is probably fine (Phase 5/6/7 will consolidate aggressively), but if it ISN'T fine and the user sees over-merging downstream, this is where to look.

### 7.12 AutoMerge — per-cluster reject in preview, not just per-group

**Status:** known UX limitation of the current preview dialog.

**Problem:** preview shows merge *groups*. User can accept/reject each group as a whole. If a group is `{2, 7, 19}` but the user wants to merge only `{2, 7}` and leave `19` alone, the only option is to reject the whole group, manually select `2` + `7`, and re-run.

**Scope:** ~80 lines. Preview dialog grows a per-cluster checkbox tree within each group. Apply step splits a partially-accepted group into accepted/rejected and only merges the accepted subset (which still needs ≥ 2 members to actually merge).

**Why it matters:** quality-of-life. Comes up naturally when scope is "All active" and the score-graph union-find produces large groups.

### 7.13 `cluster_merge_recommend.py` unification with AutoMerge

**Status:** this was the Python tooling the user has referenced in their existing pipeline.

**Idea:** the Python tool does post-KKE merge recommendation using *its own* xcorr-and-threshold logic. With AutoMerge in klusters using the same algorithm as KKE, the Python tool's role is unclear — either retire it, or wrap it as a CLI that uses the same `autoMerge.cpp` logic via a binding (would need klusters built as a library or a small standalone binary).

**Scope:** investigation first. The Python tool may do something AutoMerge doesn't (e.g., consume offline KKE log to bias the merge graph, or apply cross-session consistency checks). Don't retire blindly.

---

## 8. File map — where each feature lives

```
KKE Phase 8:
  src/klustakwikExp/KK.cpp:
    ~11445 RunPhase8VarianceSplit body
    ~11280 computeClusterTightnessRho helper
    ~4615  Phase 8 call site (in main RunChunkedCEM)
    ~6043  Phase 8 call site (in --remix-then-restart path)
  src/klustakwikExp/KK.h:
    ~295   Phase 8 method declarations
  src/klustakwikExp/KlustaKwik.cpp:
    ~510   five Phase8* flag definitions + INT_PARAM/FLOAT_PARAM registrations
  src/klustakwikExp/KlustaKwik.h:
    ~70    five Phase8* extern declarations

Klusters preferences refactor:
  src/klusters/src/pref{display,session,reclustering,refinement,automerge}.{h,cpp}
  src/klusters/src/pref{display,session,reclustering,refinement,automerge}layout.{h,cpp,ui}
  src/klusters/src/prefdialog.{h,cpp}   -- dispatch to five PrefXxx members
  src/klusters/src/icons/{display,session,reclustering,refinement,automerge}.png
  src/klusters/src/klusters-icons.qrc   -- five new entries

Klusters Auto-Merge:
  src/klusters/src/autoMerge.{h,cpp}    -- algorithm + preview dialog
  src/klusters/src/klusters.{h,cpp}:
    mAutoMerge QAction (action menu + toolbar + Shift+G)
    slotAutoMerge implementation
  src/klusters/src/configuration.{h,cpp} -- 8 new fields + QSettings round-trip
  src/klusters/src/icons/auto_merge_tool.png  -- 22x22 RGBA toolbar icon
```

---

## 9. Build + smoke-test sequence (next session)

```bash
# KKE
cd src/klustakwikExp/build && cmake .. && make -j64
./KlustaKwikExp 2>&1 | grep -i Phase8         # expect five Phase8VarianceSplit* params listed

# Klusters
cd src/klusters/build && cmake .. && make klusters -j64
# Launch klusters, open a session, then:
#   Preferences → verify 7 tabs (Display, Session, Reclustering, Refinement,
#     Auto-Merge, Cluster view, Waveform view) with icons.
#   Preferences → Auto-Merge → flip Mean ↔ Median, watch Median-K row enable/disable.
#   Select 5 clusters in palette, Shift+G → preview dialog should appear if any
#     pair scores ≥ 0.98.
#   Preferences → Auto-Merge → Restore Defaults → should match KKE flag defaults
#     (median, K=50, threshold=0.98, etc.).

# Phase 8 run (recommended starting config from §6.1)
KlustaKwikExp <session> <group> \
  -Phase4cRemixEnable 0 \
  -Phase8VarianceSplitEnable 1 \
  -Phase8VarianceSplitMaxIters 3 \
  -Phase8VarianceThreshold 0.10 \
  -Phase8VarianceSignalChannelFraction 0.1 \
  -Phase8VarianceMinClusterSize 0 \
  ...
# Watch for [Phase 8] banner and per-iter source-count progression.
```

---

## 10. Diagnostic findings worth recording

Things noticed this session that aren't bugs but are useful to know:

- **Phase 4c stuck on FullCem stragglers**: per-chunk parallelism on 36 chunks + few-per-chunk allowlist = ~30 active cores, work-imbalanced fat pools. Symptom is "stuck on a few cores" — actually completing, just slowly.
- **QualityWeightedSplit routes 0 → CEM on clean recordings**: structural consequence of min-max normalization when contam range collapses to 0. Single-cluster CEM candidate (min wavVar) gets dropped by the top-N filter. Not a bug per se; the dispatcher correctly identifies that CEM has no useful signal on this data. Coincides with Phase 8's design choice.
- **Phase 8 banner not appearing**: usually means Phase 4c is still running upstream. Disable Phase 4c to confirm Phase 8 reaches its banner.
- **`KlustersRealignAfterPhase4=1` didn't prevent 200-iter Phase 4 cap on user's session**: hypothesis (in-loop realign stops alignment-jitter splits) not fully supported by the data. Worth investigating in 7.11.

---

## 11. Reference: phase ordering today

```
Phase 1   — initial K-means seeding
Phase 2   — per-chunk DipSplit (if DipSplitEnable)
Phase 2a.5 — hull splitter / per-channel / median-subdim (if enabled; all off in v7)
Phase 2b.5 — WaveKnn split (klusters-faithful)
Phase 3   — chunked-CEM main loop start
Phase 4   — CEM iterations + 4b alternating split-merge
Phase 4b  — split step (QualityWeightedSplit dispatch + WaveKnn / FullCem)
Phase 4c  — neighborhood-remix split (if Phase4cRemixEnable)
Phase 8   — variance-targeted knn-split (if Phase8VarianceSplitEnable)   [NEW]
Phase 5   — cross-chunk consolidation
Phase 5b  — mean-subtraction merge with drift transforms
Phase 6/6b — global consolidation
Phase 7c  — klusters-faithful realignment
Phase 9   — post-Phase-4 iterated realign (KlustersRealignAfterPhase4)
```

(Phase numbers reflect historical accretion, not strict ordering. Phase 8 was the legacy global-DipSplit slot, deprecated and unused since patch ~0058; patch 0070 repurposes the number.)
