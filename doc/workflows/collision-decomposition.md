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
  ~1 ms in any Good cluster).
- Cluster boundaries are well-separated in feature space.
- Quality annotations are set on every cluster you want to be in
  the decomposition dictionary.

Decomposition relies on cluster templates being accurate. Any
cluster with mixed/contaminated waveforms produces false-positive
collision matches.

### 2. Run decomposition

```sh
ndm_decomposecollisions session.yaml
```

Outputs `session.col.N` per group. The format is binary with a
32-byte header, 32-byte parameter block, a template table, and a
record table — see [`.col.N` format](../ndmanager-plugins/formats/col.md).

For each detected spike, the decomposer:

1. Computes correlations of the candidate event against all curated
   templates (cuBLAS SGEMM batch correlation when CUDA is available).
2. Tests single-unit fits against two-unit fits (with one unit
   circularly shifted in time).
3. Records the best decomposition in `.col.N`.

For algorithm details (amplitude clamping, per-template projection
percentiles, the u1-fixed/u2-shifted convention), see
[`../design/decomposecollisions.md`](../design/decomposecollisions.md).

### 3. Visualise

```sh
collision_viewer.py session.col.N
```

Stacked Klusters-style display: raw event in grey, candidate
templates in dashed blue/orange, and the sum (model fit) in green.
Channel order is inverted (lowest channel at top) to match Klusters.
Keyboard shortcuts: A=accept, R=reject, N=next, P=prev, I=info,
D=delete.

Walk through events flagged by the decomposer and accept or reject
each. Accepted decompositions can be propagated back to the `.clu.N`
as new spike entries (one per source unit), or simply kept in the
`.col.N` for later analysis without modifying the host clu.

### 4. (Optional) Merge into the spike record

If you want collision-decomposed spikes to count as separate firings
in downstream analyses (PSTHs, cross-correlograms, etc.), use the
`--commit` flag:

```sh
ndm_decomposecollisions session.yaml --commit
```

This writes new `.res.N` and `.clu.N` entries for each accepted
decomposition. The original `.col.N` is preserved as the audit
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
rates over seconds, behavioural correlates) — the small fraction of
collided events doesn't shift those statistics.

## See also

- [`ndm_decomposecollisions`](../ndmanager-plugins/commands/ndm_decomposecollisions.md)
- [`.col.N` format](../ndmanager-plugins/formats/col.md)
- [`../design/decomposecollisions.md`](../design/decomposecollisions.md) — algorithm design
