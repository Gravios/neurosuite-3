# Modeling options for spike removal — a practical comparison

Context: this document compares the current `process_subtractspikes`
Layer 1/2 modelling against Bayes Optimal Template Matching (BOTM) as
described in Proepper 2015 (TU Berlin PhD thesis, §3.4), in light of
what you actually need from spike subtraction in the neurosuite-3
pipeline.  It concludes with three concrete, scoped upgrade options
you can choose between.

> The sciencedirect.com paper you linked
> (`pii/S1053811922001823`) was not retrievable — ScienceDirect
> blocks direct fetch and the article stem didn't resolve to a
> title via search.  If you can share the title or a preprint link
> (PMC / bioRxiv / institutional PDF), I can fold it into a
> revision of this document.

## What the current code does

`process_subtractspikes` has three modelling layers plus the new raw
mode:

- **Raw (new in this patch series)** — subtract the `.spk.N` waveform
  directly at each spike timestamp.  Near-exact cancellation.  Skips
  all fitting.  Correct choice for the re-detect workflow; produces a
  hole in the baseline at stripped spike windows that LFP would notice.

- **Layer 1 (mean + ISI burst)** — per-cluster mean template.
  Optionally splits into burst/non-burst templates with a recovery
  fit over ISI.  Per-spike scaling is L2 projection onto the
  template, clamped to `[0, 3]`.

- **Layer 2 (exponential PC1)** — PCA of clean spikes in the cluster;
  PC1 direction captured as a sum of asymmetric exponentials per
  channel; per-spike template is `T_mean + pc1_score(obs_wf) * T_pc1`.
  PC1 score is clipped to training `[0.5%, 99.5%]` percentile range.

- **Contamination-aware training**: spikes whose window overlaps any
  other spike are excluded from template estimation when enough
  clean spikes remain; falls back to trimmed-mean otherwise.

- **Neighbour cleaning at projection**: before projecting a stripped
  spike, templates of overlapping spikes from *other* clusters are
  subtracted from the observation at the overlap region.

## What's structurally missing vs BOTM

BOTM's generative model is the same one implicitly assumed by Layer
1/2, but BOTM turns the crank properly from that model:

```
xt = ΣᵢΣτ sⁱₜ₋τ · ξⁱτ + ηt
```

with `η` Gaussian-distributed, described by a covariance matrix `C`
of size `(Tf·NC)×(Tf·NC)` built from `NC×NC` Toeplitz blocks
(time correlation structure + cross-channel correlation structure).
From this generative model the Bayes-optimal decision is

```
d(i, t) = x(t)ᵀ C⁻¹ ξⁱ + cⁱ + ln p(i, t | sⁱ(t))
```

with normalisation constant `cⁱ = -½ ξⁱᵀ C⁻¹ ξⁱ`.  Three pieces
of this are missing from the current code:

1. **Noise covariance `C` is never estimated**.  The current
   projection is `x(t)ᵀ ξⁱ / ‖ξⁱ‖²` — equivalent to assuming
   `C = σ² I`, i.e. white noise, uniform across samples and
   channels.  Extracellular noise in the spike band is not white:
   there is band-limited residual LFP, common-mode artefact
   correlated across channels, and power-line interference
   correlated over ms timescales.  Ignoring `C` makes the "matched
   filter" `ξⁱ` sub-optimal — it isn't matched to the actual noise.

2. **PCA is computed on raw waveforms, not pre-whitened waveforms**.
   Proepper: *"the spikes are prewhitened by multiplying each with
   the square root of the inverse noise covariance matrix C⁻⁰·⁵.
   The prewhitening step ensures that the noise on the spikes is
   approximately standard normal distributed. ... it removes
   variance caused by noise so that the variance from waveform
   differences between units dominates, making it more likely that
   the PCs with the largest eigenvalues correspond to directions
   that separate the clusters well."*  Without pre-whitening, PC1
   of a cluster is biased toward directions along which noise has
   high variance, not the directions along which the waveform
   actually changes.

3. **The normalisation term `-½ ξⁱᵀ C⁻¹ ξⁱ` is not computed**.
   Current per-spike scaling is a heuristic `clip(projection, 0, 3)`
   plus a percentile-based PC1-score clamp.  In a Bayes-derived
   framework, the amplitude that minimises expected residual
   energy is the Bayes-weighted ratio
   `(x(t)ᵀ C⁻¹ ξⁱ) / (ξⁱᵀ C⁻¹ ξⁱ)`, and it doesn't need clamping
   under the model.

## Where overlap handling sits

The current code's "neighbour cleaning before projection" approach
handles neighbour overlap *once, at the projection step, using mean
templates of neighbour clusters*.  BOTM's equivalent is the
**Subtractive Interference Cancellation (SIC) iteration**:

1. Compute all discriminants `d(i, t)` over the data.
2. Find the globally-highest discriminant above the noise
   threshold: spike of unit `i*` at `t*`.
3. Update the discriminants to reflect that this spike has been
   subtracted.  Proepper gives three update rules (A/B/C); SIC B is
   the one equivalent to "actually subtract the found spike from
   the data and recompute":
   ```
   d^B_{i*,t*}(j, t) = d(j, t) − ξⁱ*ᵀ C⁻¹ ξʲ_τ
   ```
   with `τ = t* − t`.
4. Repeat from step 2 until no discriminants remain above threshold.

SIC B is what Proepper uses for all his analyses.  The computational
win is that each iteration only touches a `±Tf` window around the
found spike; it's not a full re-scan of the whole trace.

For very small overlaps (< ~0.3 ms) SIC B produces biased residuals
because the first-found spike is slightly misaligned.  Proepper's
**hybrid** variant uses exact overlap discriminants (enumerating
two-template combinations at small relative offsets) for those
cases, then SIC B for the rest.  Reported improvement: 1.7% →
1.0% error rate on simulated overlaps.

**How this maps to your workflow**: the neurosuite iterative
strip-good / re-detect / curate / strip-more loop is *structurally
the same as SIC*, at a larger granularity with an operator in the
loop.  Each iteration of your loop is one SIC step — you find the
spikes you're confident in, subtract them, find what was hidden
under them, repeat.  The within-iteration use of SIC (automatic,
one-pass, inside `process_subtractspikes`) would complement your
manual loop for overlapping spikes *within the same cluster* or
with small inter-spike offsets, neither of which your current outer
loop handles.

## Concrete upgrade options

Three options, ordered by scope.  Use exactly one — they're not
cumulative.

### Option A — Pre-whitening retrofit (smallest patch)

Keep the current Layer 1/2 structure.  Estimate `C` and apply
`C⁻⁰·⁵` pre-whitening where it matters:

- Once per group at load time, compute `C` from between-spike
  samples using Proepper's per-period weighted average: *"C is
  estimated separately for each noise period and the estimations
  are averaged, weighted by the length of their respective period"*
  — this avoids the stitching pitfall where samples at period
  borders that were originally far apart get treated as adjacent.
- Cholesky-factor `C` once to produce `L` such that `C = L Lᵀ`.
  Pre-whiten a waveform by solving `L y = x` (triangular solve).
- Apply pre-whitening **before PCA** when building
  `ExponentialWaveformModel`, so PC1 captures shape variance not
  noise variance.
- Apply pre-whitening to both the template and observation when
  computing the per-spike projection coefficient in Layer 1 —
  this replaces the current
  `clip(dot(wf, tmpl) / dot(tmpl, tmpl), 0, 3)`
  with the Bayes-weighted
  `(Lˉ¹ wf)ᵀ (Lˉ¹ tmpl) / ‖Lˉ¹ tmpl‖²`
  (clamping can then be dropped or loosened).

**Expected result**: meaningfully better Layer 2 fit quality
(the ~30% model-mode residual you're seeing on clean spikes should
drop substantially, because PC1 will align with waveform-shape
variance rather than noise-variance directions).  Contamination-
aware training and neighbour cleaning stay as they are.

**Scope**: ~100 lines of Python inside `process_subtractspikes`.
New optional parameter `useWhitening: true|false`.  No API change
to `ndm_stripdat`.  Adds a small one-shot computation per group
(Cholesky of a ~(32·Nch)² matrix is trivial — ~1 ms).  Does not
affect raw mode.

**Limitation**: still per-spike, still operates at the
observation → template projection level.  Overlap handling remains
the current "subtract neighbour means from observation first"
approach.  No change to the iterative workflow.

### Option B — Add `botm` as a third subtraction mode

Alongside `model` (Layer 1/2) and `raw`, add `botm`.  In this mode:

- Build templates and `C` at load time, same as Option A.
- Compute per-template matched filter `fⁱ = C⁻¹ ξⁱ` and
  normalisation `cⁱ = -½ ξⁱᵀ C⁻¹ ξⁱ`.
- For each stripped spike, use Bayes-weighted amplitude
  (no clamp): `a = (x(t)ᵀ fⁱ − cⁱ) / (ξⁱᵀ fⁱ)` — this is the
  optimal scale under the model.
- Optionally: inside `process_subtractspikes`, run one pass of
  SIC B over the spikes per group, updating `a` for each
  overlapping spike pair by the cross-term `ξⁱᵀ C⁻¹ ξʲ_τ`.
  This tightens within-group overlap handling without changing
  the outer operator loop.

**Expected result**: model-mode residuals approach raw-mode (near
zero) when the templates are well-formed, *and* the noise baseline
is preserved at spike windows — Raw's main weakness as an LFP
input.  Overlap handling becomes principled instead of
heuristic.

**Scope**: ~300 lines of Python; new mode, new model-build code
path, new per-spike subtraction code path.  All optional — the
existing `model` and `raw` modes remain unchanged.  YAML
configurable as `subtractionMode: "botm"`.  Still no need to
touch the C++ pipeline.

**Limitation**: SIC B inside `process_subtractspikes` gives you
one automatic subtract-and-rescan pass per cluster.  It does
*not* replace your operator-in-the-loop iterative workflow —
that remains the right tool for finding genuinely new units
masked by confirmed ones.  SIC B catches overlaps between
already-sorted units that the current code misaligns.

### Option C — Full BOTM replacement (largest patch, not recommended yet)

Port the whole subtraction path to the BOTM formalism with hybrid
overlap discriminants.  Proepper's published implementation
(BOTMpy, at the time of his thesis) is the reference.

**Why not yet**: this is invasive.  It changes the semantics of
`process_subtractspikes` from "I have a list of sorted spikes,
subtract them" to "I have templates, find-and-subtract spikes".
In the current neurosuite pipeline, sorting happens in
`process_klustakwik` and the sorted `.clu` is the input to
stripping — BOTM inverts that.  There's a real argument for
eventually moving to a sort-and-subtract unified path à la
Kilosort, but it's a different project.

If you want BOTM-style sort-and-subtract, Kilosort4, SpyKING
Circus, and Mountainsort5 all already do it and integrate via
SpikeInterface wrappers.  Adding your own would duplicate work
unless neurosuite's operator-in-the-loop model is worth keeping
(which I think it is — but that's a separate architectural
discussion).

## Recommendation

**Do Option A first.**  It closes the most embarrassing gap (PCA
computed on noise-biased data) with a small, scoped patch.
Measure the residual improvement on your real data.  Decide
whether Option B is worth pursuing based on whether (a) model-mode
baseline preservation matters to you for LFP, and (b) within-group
overlaps are meaningfully misaligned in the current code.

Option C is best left for when the whole pipeline gets revisited
— possibly as part of the future C++ port of
`process_subtractspikes`.

The existing **`raw` subtraction mode is already the right answer
for the iterative workflow.**  The entire conversation about
whether to upgrade the modelling is really about model-mode:
whether `subtractionMode: "model"` should produce LFP-quality
residuals or not.  If it doesn't need to, you can defer all of
this indefinitely and use raw for everything.

## Key references

- Proepper, R. (2015).  *New tools for electrophysiological data
  analysis and their application to a working memory study.*
  PhD dissertation, TU Berlin.  §3.4 Bayes Optimal Template
  Matching, §3.4.4 Overlap resolution (SIC A/B/C + hybrid),
  §3.4.2 Initialization step (covariance estimation pitfalls,
  pre-whitening rationale).
- Franke, F. (2011).  *Real-time analysis of extracellular
  multielectrode recordings.*  PhD dissertation.  Original derivation
  of BOTM; LDA connection; SIC A formulation (the one with the
  extra prior term Proepper later argued against).
- Pouzat, C., Mazor, O., Laurent, G. (2002).  "Using noise signature
  to optimize spike-sorting and to assess neuronal classification
  quality."  *J Neurosci Methods* 122: 43-57.  Original generative
  model; noise covariance estimation for spike sorting.
- NeuroImage 2022, pii S1053811922001823 — **paper not retrievable
  in this session; deferred.**
