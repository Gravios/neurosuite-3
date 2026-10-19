# Collision decomposition

Resolves overlapping spikes — events where two units fire close enough
in time that their waveforms superimpose and the detector picks up a
single distorted spike rather than two separate ones. The output
attributes each detected event to one or two source units with their
estimated time offsets and amplitudes.

Run this *after* curation. Decomposition uses curated cluster
templates as the dictionary of candidate sources, so it's only useful
once you trust the cluster set.

## Steps

### 1. Verify clean curation

Open the relevant shank in Klusters. Check that:

- Refractory periods are clean (autocorrelograms with no peak under
  ~1 ms in any well-isolated cluster).
- Cluster boundaries are well-separated in feature space.
- Every cluster you do not want as a template is in noise (1) or
  artefact (0).

Every cluster except 0 and 1 (with `excludeNoise: true`, the default)
that has at least `minSpikesTemplate` spikes (default 30) becomes a
template; quality annotations are not consulted. Decomposition relies
on those templates being accurate. Any cluster with mixed/contaminated
waveforms produces false-positive collision matches.

### 2. Run decomposition

Two routes run the same engine (`process_decomposecollisions`).

**In Klusters — the open group.** With the group open, run *Plugins ▸
process_decomposecollisions*. Session, method, group, waveform geometry and the
open stage's clu tag are filled in from the open group; only the thresholds are
yours to set. Klusters asks before it closes and reopens the group, then
applies the accepted decompositions itself (*batch decollide*, step 4).

**From the shell — every group.**

```sh
ndm_decomposecollisions session.yaml [--method M]
```

Outputs `session.col.<method>.N` per group (a stage-tagged run:
`session.col.<method>.N.<tag>`), and `.eap` / `.tcl` membership for groups with
an accepted decomposition. The `.col` format is binary — a 32-byte header,
32-byte parameter block, a template table and a record table — see
[`.col`](../ndmanager-plugins/formats/col.md). `.clu`, `.res` and `.spk` are
never modified.

For each detected spike, the decomposer:

1. Computes correlations of the candidate event against all curated
   templates (cuBLAS SGEMM batch correlation when CUDA is available).
2. Tests single-unit fits against two-unit fits (with one unit
   circularly shifted in time).
3. Records the best decomposition in the `.col`, flagged *accepted* when it
   passes the correlation, residual and amplitude thresholds.

For algorithm details (amplitude clamping, per-template projection
percentiles, the u1-fixed/u2-shifted convention), see
[`../design/decomposecollisions.md`](../design/decomposecollisions.md) and the
[command reference](../ndmanager-plugins/commands/ndm_decomposecollisions.md).

### 3. Visualise

```sh
collision_viewer.py SESSION [GROUP]
```

Finds `SESSION.col.<method>.N` itself. Stacked Klusters-style display: raw event
in gray, the two component fits in blue/orange, their sum (the model fit)
dashed green, the residual in red; a threshold panel plots residual against
best single-unit correlation for the current unit pair. Channel order is
inverted (lowest channel at top) to match Klusters. Keys: A = accept,
R = reject, N = next, P = previous, I / D = scale the waveforms up / down.

Your accept / reject decisions are saved to `SESSION_decisions.csv`; they do
not change the `.col`, so they are a review record (and a guide for retuning the
thresholds), not an input to step 4.

### 4. (Optional) Fold the decompositions into the spike record

To count collision-decomposed spikes as separate firings in downstream analyses
(PSTHs, cross-correlograms, …), apply the `.col` in Klusters. The Klusters route
in step 2 does this automatically; it can also be done for a lasso of spikes
against a pinned two-cluster oblique basis (*decollide*, key D while a lasso is
pending). Either way Klusters

- records the decompositions as `.eap` / `.tcl` membership on the current
  stage, and
- writes a **new stage** whose `.res` / `.clu` / `.spk` / `.fet` carry the second
  unit of each accepted decomposition as its own spike, and opens it.

The stage you started from is left as it was, and the `.col` stays as the audit
trail.

## When this matters

Collision decomposition is most impactful when:

- Two units share a channel (similar waveforms, hard to separate by
  feature-space alone).
- Both units have high firing rates (lots of opportunities for
  collision).
- You're computing fine-timescale cross-correlograms and need every
  spike attributed correctly.

It's rarely worth the time for analyses at coarser timescales (firing
rates over seconds, behavioral correlates) — the small fraction of
collided events doesn't shift those statistics.

## See also

- [`ndm_decomposecollisions`](../ndmanager-plugins/commands/ndm_decomposecollisions.md)
- [`.col` format](../ndmanager-plugins/formats/col.md)
- [`../design/decomposecollisions.md`](../design/decomposecollisions.md) — algorithm design
