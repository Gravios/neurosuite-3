# Workflows

Task-oriented walkthroughs that span multiple programs in the
neurosuite-3 toolchain. Each workflow describes a recipe — what you
want to accomplish — and links out to the per-program reference docs
for the details of each step.

| Workflow | Use when |
|---|---|
| [First-time spike sort](first-time-sort.md) | New recording: acquire → preprocess → KiloKlustaKwik → curate |
| [Cluster curation](cluster-curation.md) | Working through a `.clu.N` in Klusters: split / merge / realign / nudge / dipsplit |
| [Re-extract with a lower threshold](re-extract-lower-threshold.md) | Recover weak units missed by the first detection pass |
| [Iterative refinement](iterative-refinement.md) | Strip → redetect → re-sort loop for cleaner separation |
| [Empirical priors](empirical-priors.md) | Train per-probe KiloKlustaKwik defaults from accumulated curation logs |
| [Drift correction](drift-correction.md) | Estimate probe drift from one curated shank, propagate to siblings |
| [Collision decomposition](collision-decomposition.md) | Resolve overlapping spikes after curation |

For per-program reference, see:

- [`../klusters/README.md`](../klusters/README.md)
- [`../kiloklustakwik/README.md`](../kiloklustakwik/README.md)
- [`../ndmanager/README.md`](../ndmanager/README.md)
- [`../ndmanager-plugins/README.md`](../ndmanager-plugins/README.md) (with [per-command pages](../ndmanager-plugins/commands/))
- [`../neuroscope/README.md`](../neuroscope/README.md)
