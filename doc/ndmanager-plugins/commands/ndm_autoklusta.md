# `ndm_autoklusta` — learned cluster curation (`.clu.N.<raw>` → `.clu.N.autocur`)

**Contents:** [Synopsis](#synopsis) · [Description](#description) ·
[Modes](#modes) · [Model](#model) · [Curation log](#curation-log-split-supervision) ·
[Parameters](#parameters) · [Example](#example)

## Synopsis

```bash
ndm_autoklusta [session.yaml] [group]
```

When `session.yaml` is omitted, the current directory name is used as
the session basename. A `group` argument restricts the run to that
1-based spike group and overrides the `group` parameter.

**Input:** raw cluster file `SESSION.clu.N.<rawCluExt>`; in `train` and
`eval` also the curated `SESSION.clu.N.<curatedCluExt>`; per group the
feature file, optionally `.res`, `.spk` and a Klusters curation log;
the session YAML (required, no XML fallback)

**Output:** `train` — the model file (see [Model](#model)); `apply` —
`SESSION.clu.N.<outCluExt>` (default `SESSION.clu.N.autocur`) per group,
plus `SESSION.split_candidates.N` when the model flags clusters; `eval`
— a report on stdout

## Description

When you curate KlustaKwik output in Klusters you discard junk clusters
to noise/MUA, merge over-split fragments of one unit, and keep clean
clusters. The raw and the curated `.clu` index the same spikes, so
together they record those decisions. `ndm_autoklusta` recovers the
decisions, trains classifiers on them, and replays them on un-curated
output. The work is done by `process_autoklusta.py`; the bash driver
reads the parameters and the per-group geometry (`nSamples`, channel
count) from the YAML.

Requires `python3` with `pyyaml`, `numpy`, `scipy`, `scikit-learn` and
`joblib`.

### Decisions recovered

For every raw cluster that is not a noise cluster (`noiseClusters`,
default 0 and 1), the curated cluster holding most of its spikes is its
fate:

| Decision | Rule |
|---|---|
| DISCARD | the majority went to a noise cluster |
| KEEP | the majority went to a real curated unit |
| MERGE | two kept raw clusters whose majority is the same curated unit |

Manual splits (one raw cluster spread over several curated units)
cannot be replayed from cluster-level features. In `train` a kept
cluster whose largest curated share is below `splitPurity` is reported
as a likely human split. Split supervision comes only from the
[curation log](#curation-log-split-supervision).

### Cluster-file names

The three `*CluExt` parameters are Klusters labels in
`SESSION.clu.N.<label>` (group number before the label), e.g.
`rawCluExt: K` reads `SESSION.clu.5.K` and `curatedCluExt: final` reads
`SESSION.clu.5.final`. An empty label means the plain `SESSION.clu.N`.
These names do not follow the `SESSION.clu.<method>.N` scheme that
[`ndm_klustakwik`](ndm_klustakwik.md) writes: save or copy the raw sort
under a label (e.g. *Save As* `.clu.N.K` in Klusters) before training
or applying.

The other per-group inputs are resolved by preference, first existing
file wins:

- features: `.fet.stderiv.N`, `.fet.D.N`, `.fetD.N`, `.fet.N` (required);
- waveforms (when `useWaveforms` is true): same order with `spk`;
  if missing or too short, waveform features are left empty and a
  message explains why;
- spike times: `.res.N`, `.res.stderiv.N`, `.res.D.N`, `.resD.N`; if
  none exists the timestamp column of the feature file is used.

Method-tagged `standard` or `sdiff` files (`.fet.standard.N`, …) are
not among the candidates.

Groups missing a required file are skipped. If the files of a group
have different spike counts, all are truncated to the shortest with a
warning.

## Modes

**`train`** — for every selected group with both a raw and a curated
`.clu`, extracts one row per raw cluster (label: keep/discard) and one
row per pair of kept clusters (label: merged or not), appends them to
the model's training store, and refits the classifiers on the whole
store. Groups are identified as `<session basename>:<group>`; a group
already in the store is skipped unless `force: true`. `reset: true`
starts a new store. Raw and curated must resolve to different files,
otherwise the run stops with an error. The cluster-level and pair-level
classifiers report grouped cross-validation (folds by
`session:group`) when there are at least two groups and two examples
per class.

**`apply`** — needs a trained model. Per group:

1. a raw cluster is kept when p(keep) ≥ `keepThreshold`, otherwise its
   spikes go to cluster 1 (MUA);
2. every pair of kept clusters with p(merge) ≥ `mergeThreshold` is
   linked, and each connected component becomes one unit;
3. units are renumbered from 2, ordered by their earliest spike; spikes
   in raw noise clusters keep their original IDs;
4. if the model has a split classifier, kept clusters with
   p(split) ≥ `splitThreshold` are listed in
   `SESSION.split_candidates.N` (`cluster  p_split`) for review in
   Klusters. They are not split.

The original `SESSION.clu.N` is never overwritten unless `outCluExt` is
set empty. If an output file already exists for any selected group, the
whole run is skipped (exit status 10).

**`eval`** — runs the same replay on groups that also have a curated
`.clu` and prints, per group, the human and model unit counts, keep
TP/TN/FP/FN with precision and recall, and the spike-level adjusted
Rand index against the curation. Nothing is written.

## Model

The model is a `joblib` bundle holding the classifiers
(scikit-learn `HistGradientBoostingClassifier`, class-balanced, NaN
tolerant; a constant predictor when only one class has been seen), the
training store, and the configuration used at training time
(`noiseClusters`, `refractoryMs`, `splitPurity` and the thresholds).

Its location is, in order:

1. `modelPath`, if set (`~` is expanded; a directory, or a path ending
   in `/`, gets `<modelKey>.curatemodel.joblib` inside it);
2. `$NEUROSUITE_AUTOKLUSTA_MODEL`;
3. `${XDG_DATA_HOME:-~/.local/share}/neurosuite/autoklusta/<modelKey>.curatemodel.joblib`.

Use `modelKey` to keep separate models, e.g. per probe type. Because
training accumulates, re-running `train` on further sessions or shanks
improves the same model without re-reading earlier data.

All inputs are scale-invariant scalars, so one model applies across
groups, shanks and probes with different feature counts:

- **per cluster** — spike count, rate and fraction; isolation distance,
  L-ratio and ISI-violation fraction; Mahalanobis radius; spread
  relative to the whole shank; covariance anisotropy, participation
  ratio and top-eigenvalue fraction; Mahalanobis gap to the nearest
  unit and crowding; waveform SNR and channel concentration (optional).
- **per pair** — symmetric and shank-whitened Mahalanobis separation;
  Bhattacharyya overlap; alignment of the centroid join with each
  cluster's principal axis (over-split fragments separate along their
  elongation); gap ratio; size ratio; temporal-span overlap;
  cross-refractory coincidences and ISI violations of the merged train;
  waveform cosine and peak-to-peak ratio (optional).

### Thresholds

`keepThreshold`, `mergeThreshold` and `splitThreshold` are
probabilities. A negative value (the default, `-1`) means "use the
value stored in the model"; `train` stores the value you give, or 0.5
when it is negative. Raise `keepThreshold` to discard more clusters;
raise `mergeThreshold` to merge less; raise `splitThreshold` to flag
fewer split candidates. `eval` uses `keepThreshold` and
`mergeThreshold` only.

## Curation log (split supervision)

With `curationLog` set, `train` also reads the Klusters curation log of
each group (`SESSION.curation_log.<group>.<method>`; see
[curation logging](../../klusters/README.md#curation-logging)) and fits
a third, split classifier:

- the log is appended across Klusters sessions; only the last block of
  sessions whose cluster counts chain from one session to the next is
  used, as earlier blocks belong to abandoned sorts;
- each pre-action cluster snapshot is an example: label 1 when an
  algorithmic split (`SPLIT`, `SPLIT_N`) of that cluster was accepted,
  0 otherwise; `GROUP` and delete actions are only counted in the
  report;
- the features are the quality metrics Klusters logs (ISI statistics,
  feature-variance and drift measures, L-ratio, isolation distance,
  waveform shape); at `apply` time they are recomputed from the
  `.fet`/`.spk` of each kept cluster.

`curationLog` values: empty or `false` — off; `true` or `auto` — the
first `SESSION.curation_log.<group>.*` next to the session (`.bak`
files ignored); a directory — the same pattern in that directory;
anything else — a file path, where `{g}` is replaced by the group
number. A log is ingested once per group (again with `force: true`).

## Parameters

All parameters are optional and read from the `ndm_autoklusta` block;
see [parameters](../parameters.md).

| Parameter | Default | Meaning |
|---|---|---|
| `mode` | `apply` | `train`, `apply` or `eval`. |
| `group` | *(empty)* | Single 1-based group to process; empty = all. The CLI argument wins. |
| `rawCluExt` | *(empty)* | Label of the raw `.clu` (`SESSION.clu.N.<label>`); empty = `SESSION.clu.N`. |
| `curatedCluExt` | *(empty)* | Label of the curated `.clu` (train/eval); empty = `SESSION.clu.N`. Must differ from `rawCluExt` in `train`. |
| `outCluExt` | `autocur` | Label of the `.clu` written by `apply`; empty = overwrite `SESSION.clu.N`. |
| `modelKey` | `default` | Model file stem; selects one of several models. |
| `modelPath` | *(empty)* | Explicit model file or directory (see [Model](#model)). |
| `noiseClusters` | `0,1` | Curated cluster IDs counted as noise/artifact. |
| `keepThreshold` | `-1` | p(keep) cutoff; negative = value stored in the model. |
| `mergeThreshold` | `-1` | p(merge) cutoff; negative = value stored in the model. |
| `splitThreshold` | `-1` | p(split) cutoff for split candidates; negative = stored value, else 0.5. |
| `splitPurity` | `0.8` | `train`: kept clusters with a smaller top curated share are reported as likely splits. |
| `useWaveforms` | `true` | Add waveform features when a `.spk` file matches the group geometry. |
| `refractoryMs` | `2.0` | Refractory window for ISI-violation features. |
| `reset` | `false` | `train`: start a fresh model store. |
| `force` | `false` | `train`: re-ingest groups (and curation logs) already in the store. |
| `curationLog` | *(empty)* | Curation log for split training (see above). |
| `probeLibrary` | *(empty)* | Accepted and passed on, but not used; geometry comes from the YAML. |

## Example

```yaml
- name: ndm_autoklusta
  parameters:
  - {name: mode,          value: train, status: Optional}
  - {name: rawCluExt,     value: K,     status: Optional}
  - {name: curatedCluExt, value: final, status: Optional}
  - {name: modelKey,      value: a32,   status: Optional}
  - {name: curationLog,   value: auto,  status: Optional}
```

```bash
ndm_autoklusta session.yaml        # train on every group with .clu.N.K + .clu.N.final
ndm_autoklusta session.yaml 5      # train on group 5 only
# on a new session, with mode: apply and rawCluExt: K
ndm_autoklusta new_session.yaml    # writes new_session.clu.N.autocur
```

## See also

- [`ndm_klustakwik`](ndm_klustakwik.md) — produces the raw sort
- [Cluster curation workflow](../../workflows/cluster-curation.md)
- [Iterative refinement](../../workflows/iterative-refinement.md)
- [`.clu` format](../formats/clu.md), [file naming](../formats/naming.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
