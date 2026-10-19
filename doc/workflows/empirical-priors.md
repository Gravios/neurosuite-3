# KiloKlustaKwik per-probe prior workflow

The `kk_build_prior.py` / `kk_resolve_prior.py` pair lets you train
empirical KiloKlustaKwik priors once per probe-type-and-shank, then reuse
them silently across sessions, animals, and re-extraction passes.
Priors feed the headline KiloKlustaKwik parameters
(`MinClusters`, `MaxClusters`, `MergeThresh`, `PenaltyMix`) used by
`ndm_subcluster_unmatched`; any value set in the session YAML still
wins (see [Phase 3](#phase-3--reuse-the-prior-automatically)).

This document is the operational counterpart to the per-script
descriptions in [ndmanager-plugins README](../ndmanager-plugins/README.md).
For the CHANGES log entry, see
[`../design/kk-prior.md`](../design/kk-prior.md).

**Contents:** [Concepts](#concepts) · [Search paths](#search-paths) · [Phase 1 — first-time bootstrap](#phase-1--first-time-bootstrap) · [Phase 2 — build the first prior](#phase-2--build-the-first-prior) · [Phase 3 — reuse the prior automatically](#phase-3--reuse-the-prior-automatically) · [Phase 4 — lower-threshold re-extract](#phase-4--lower-threshold-re-extract) · [Phase 5 — onboarding a new animal on the same probe](#phase-5--onboarding-a-new-animal-on-the-same-probe) · [Phase 6 — refreshing priors](#phase-6--refreshing-priors) · [Verifying a prior is being used](#verifying-a-prior-is-being-used) · [Troubleshooting](#troubleshooting)

---

## Concepts

### Probe identity

A **probe identity** is computed from the session YAML's channel-group
structure: per group, the sorted channel ids, `nSamples`,
`peakSampleIndex`, and `nFeatures`, plus the total `nChannels`. Two
sessions with the same channel layout produce the same identity — same
shank topology, same spike-window length, same PCA depth ⇒ same probe.

`samplingRate` is **not** part of the signature (it lives in
`acquisitionSystem`, not in the probe definition). Two sessions on the
same physical probe at different rates resolve to different priors only
if they end up with different `nSamples` (which they normally do — a
2 ms window is 64 samples at 32 kHz and 40 samples at 20 kHz).

### Friendly names vs. hex hashes

When the signature exactly matches one of the installed
`Template-*.yaml` files (see [Search paths](#search-paths)), the identity gets a friendly name like
`4-octrodes-32552Hz`. Otherwise it falls back to a 16-hex-char SHA-256
hash like `46b75788e3de5d6a`. Both are stored in every prior
(`probe_id` and `probe_signature_hash`); the resolver validates against
the hash so a renamed file can never be silently misapplied.

### Per shank, not per probe

Each prior is for one electrode group on one probe type. The naming
convention is `<probe_id>.<group>.prior.yaml`. A single probe with four
shanks produces four prior files. Different shanks see different cell
populations and have different cluster-count distributions, so the
calibration is genuinely per-shank.

---

## Search paths

`kk_resolve_prior.py` walks these directories in order, returning the
first file whose contents validate against the session's signature
hash:

| Order | Path | Use |
|---|---|---|
| 1 | `$NDM_PRIOR_DIR` (colon-separated) | Project-scoped overrides |
| 2 | `~/.ndm/priors` | Per-user store *(typical home)* |
| 3 | `/etc/ndm/priors` | Lab-wide shared store |
| 4 | The session yaml's directory | Session-local override |

Template lookup (for friendly names) searches `$NDM_TEMPLATE_DIR`
(colon-separated), then `/usr/share/ndmanager/templates` and
`/usr/local/share/ndmanager/templates`, then the source tree next to the
scripts. ndmanager installs its templates to
`<prefix>/share/ndmanager/templates`, so with an install prefix other
than `/usr` or `/usr/local` (for example `~/.local` or a conda
environment) set the variable before building or resolving priors:

```sh
export NDM_TEMPLATE_DIR=$HOME/.local/share/ndmanager/templates
```

Use the same setting for `kk_build_prior.py` and every later run:
the friendly name becomes part of the prior's file name, so a prior
built with the templates found is not found by a resolver that falls
back to the hex hash, and vice versa.

---

## Phase 1 — first-time bootstrap

Run `ndm_subcluster_unmatched` (or `ndm_klustakwik`) on the first
session as you always have. The resolver will look for a prior, find
none, and silently fall through to the in-script defaults:

```sh
ndm_subcluster_unmatched session_a.yaml --method stderiv
```

Curate the result in Klusters as usual: split, merge, nudge, recluster.
`kk_build_prior.py` keeps a logged cluster snapshot when the action was
a group, split, recluster, realign or nudge that was not undone, and
the cluster passes the structural filters (`--min-spikes`,
`--max-l-ratio`, `--min-isolation`). The quality you type into the
cluster information dialog is not logged and does not affect the prior.
Klusters keeps a curation log only for non-`standard` methods.

When you save and close, `session_a.curation_log.<group>.<method>` accumulates
one JSON-line record per action. See
[klusters/README.md](../klusters/README.md) for the
log schema.

Repeat across two or three sessions on the same shank. You don't need
many — 100–200 well-curated clusters is enough to stabilise the
distribution stats.

---

## Phase 2 — build the first prior

For each shank, point `kk_build_prior.py` at the curation logs and a
representative session YAML:

```sh
mkdir -p ~/.ndm/priors

kk_build_prior.py \
    /data/session_a/session_a.curation_log.7.stderiv \
    /data/session_b/session_b.curation_log.7.stderiv \
    /data/session_c/session_c.curation_log.7.stderiv \
    --session-yaml /data/session_a/session_a.yaml \
    --electrode-group 7 \
    --out-dir ~/.ndm/priors
```

Output (when the session matches a template):

```
Probe id: 4-octrodes-32552Hz  (hash: 79899eea3d42439f)
  groups: 4, n_channels: 32
Output:   /home/<you>/.ndm/priors/4-octrodes-32552Hz.7.prior.yaml

[...filter & analysis output...]

KiloKlustaKwik will apply (when -PriorFile is set):
  MinClusters  <- 42
  MaxClusters  <- 78
  MergeThresh  <- 55.0  (chi2 at median d_eff=21.5)
  PenaltyMix   <- 0.030
```

`--session-yaml` is used **only** to compute the probe identity; the
curation logs themselves carry the analytical content. Any session
recorded on the same probe works — pick the most recent for chronic
recordings.

Repeat per shank. The result:

```
~/.ndm/priors/
  4-octrodes-32552Hz.1.prior.yaml
  4-octrodes-32552Hz.2.prior.yaml
  4-octrodes-32552Hz.3.prior.yaml
  4-octrodes-32552Hz.4.prior.yaml
```

---

## Phase 3 — reuse the prior automatically

`ndm_subcluster_unmatched` invokes `kk_resolve_prior.py` for every
shank as it runs. **No changes to your invocation:**

```sh
ndm_subcluster_unmatched session_d.yaml
```

For each group it prints:

```
  group 7: prior /home/<you>/.ndm/priors/4-octrodes-32552Hz.7.prior.yaml
    minC=42 maxC=78 merge=55.0 penaltyMix=0.030
```

Those four values replace the script's built-in defaults. Anything set
in the session YAML still wins. The full priority order is:

1. `spikeDetection.channelGroups.group[N].klustakwik.<name>` — per
   shank (highest priority)
2. `programs[ndm_subcluster_unmatched].parameters.<name>`
3. `programs[ndm_klustakwik].parameters.<name>`
4. The per-shank prior file value (probe-typical default)
5. The script's built-in default (used if no prior matches)

Note that a value in the `ndm_klustakwik` program block (3) hides the
prior, so leave `minClusters`, `maxClusters`, `mergeThresh` and
`penaltyMix` out of that block if you want the prior to apply.

If no prior matches (different probe, no curation logs yet for this
shank), the resolver returns nothing and the script falls through
to its built-in defaults — exactly as before. **No prior == old
behavior preserved.**

---

## Phase 4 — lower-threshold re-extract

This is the workflow you typically reach for to recover weak units:

```sh
# 1. Detect the spikes the first pass missed (threshold =
#    reextractThresholdFactor, default 0.75 × thresholdFactor) and
#    shadow-cluster them into the curated sort
ndm_reextractspikes session_d.yaml --method stderiv

# 2. Sub-cluster the unmatched bin.  The existing prior helps even
#    though it was trained on the original (higher-threshold) residuals
ndm_subcluster_unmatched session_d.yaml --method stderiv
```

See [Re-extract with a lower threshold](re-extract-lower-threshold.md)
for the full recipe.

The prior's `MergeThresh` is the most-transferable number — it's
determined by cluster geometry (d_eff), which depends on probe layout,
not on extraction threshold. `MaxClusters` may be slightly low (lower
threshold ⇒ more weak residuals ⇒ more clusters), so if the recluster
hits the cap, raise it for that shank in the session YAML:

```yaml
# session_d.yaml — under spikeDetection.channelGroups.group[N]
klustakwik:
  maxClusters: 120
```

A per-shank `klustakwik` value wins over the prior; note that
`ndm_klustakwik` reads the same per-shank keys.

After curating the lower-threshold sort, regenerate the prior so
future runs use the recalibrated values. The new YAML overwrites the
old one in place:

```sh
kk_build_prior.py \
    /data/session_*/session_*.curation_log.7.stderiv \
    --session-yaml /data/session_a/session_a.yaml \
    --electrode-group 7 \
    --out-dir ~/.ndm/priors
```

---

## Phase 5 — onboarding a new animal on the same probe

No work required. The new animal's session YAML is structurally
identical to the existing ones (same template), so the same prior
resolves automatically:

```sh
ndm_subcluster_unmatched new_rat.yaml --method stderiv
# → group 7: prior /home/<you>/.ndm/priors/4-octrodes-32552Hz.7.prior.yaml
```

If the new probe is a different template (e.g. `8-octrodes-32552Hz`),
the resolver silently misses and falls back to defaults until you've
curated enough on the new probe to build its own prior. Repeat phases
1–2 for the new probe; the old priors continue to apply to old probes.

---

## Phase 6 — refreshing priors

Rebuild a prior when:

- You re-extract with a different amplitude threshold and accept the
  resulting sort.
- You change `nFeatures` or `nSamples` in the session config (this
  produces a new probe identity, so the old prior is no longer
  resolved rather than stale — but the old YAML remains for the old
  config).
- The recluster routinely hits `MaxClusters` or `MinClusters` — the
  cluster-count distribution has shifted.
- You've curated enough additional sessions that the underlying
  distributions might have meaningfully drifted (rule of thumb: every
  ~10 well-curated sessions).

You don't need to rebuild on a schedule. A prior that was correct
when generated stays correct as long as the recording configuration is
unchanged — they're not perishable.

---

## Verifying a prior is being used

To check resolution without running the full pipeline:

```sh
kk_resolve_prior.py --session /data/session_x/session_x.yaml --group 7 -v
```

Successful output (path on stdout, trace on stderr):

```
# probe_id   = 4-octrodes-32552Hz
# sig_hash   = 79899eea3d42439f
# looking for 4-octrodes-32552Hz.7.prior.yaml
/home/<you>/.ndm/priors/4-octrodes-32552Hz.7.prior.yaml
```

Exit codes:

| Code | Meaning |
|---|---|
| 0 | Resolved (path on stdout) |
| 1 | No matching prior found |
| 2 | Bad arguments / unreadable session yaml |
| 3 | Filename matched but `probe_signature_hash` inside disagrees |

Code 3 is fail-loud: a prior file with a hash mismatch is never used
silently.

---

## Troubleshooting

<details>
<summary>Symptoms and fixes</summary>

**"No matching prior found" but I built one for this probe.**
Compare the probe ids:

```sh
kk_resolve_prior.py --session this_session.yaml --group 7 --print-id
```

If the printed id differs from the prior's filename, your session has
a slightly different channel layout than the session you used to build
the prior. Diff the `spikeDetection.channelGroups` sections of the two
session YAMLs.

**"probe_signature_hash mismatch" — exit 3.**
The prior's filename matches but its inner hash disagrees with what the
session computes. Either (a) someone renamed the prior file without
rebuilding it, (b) the session YAML was edited after the prior was
built, or (c) two different probes happened to collide on a friendly
name (extremely unlikely). Rebuild the prior from the original
curation logs, or verify the session config matches what was used at
build time.

**Resolver works, but `MergeThresh` looks too high.**
Most likely the prior was built from a session at higher amplitude
threshold (compact, well-isolated clusters → low d_eff → low merge
threshold). Building from a lower-threshold curation pass will
naturally raise `MergeThresh` because residual clusters are more
diffuse — that's correct behavior. Set `mergeThresh` under the shank's
`klustakwik` block in the session YAML if you want to clamp it for one
recording.

**Friendly name didn't apply, got hex hash instead.**
Either no `Template-*.yaml` matches the session's signature, or the
templates aren't on the search path (an install prefix other than
`/usr` or `/usr/local` needs `NDM_TEMPLATE_DIR`; see
[Search paths](#search-paths)). Check `$NDM_TEMPLATE_DIR`,
`/usr/share/ndmanager/templates`, and
`/usr/local/share/ndmanager/templates`, or pass `--templates-dir`
explicitly to `kk_build_prior.py` and `kk_resolve_prior.py`
(`ndm_subcluster_unmatched` calls the resolver without it, so it relies
on the environment variable).
A hex-id prior works just as well; the friendly name is convenience.

**Ndm_klustakwik doesn't pick up the prior.**
Currently only `ndm_subcluster_unmatched` consumes priors. The shape
of `ndm_klustakwik`'s parameter resolution is similar; extending it to
read priors is a straightforward follow-on. Open an issue if you'd
like this added.

</details>
