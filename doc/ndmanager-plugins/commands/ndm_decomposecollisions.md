# `ndm_decomposecollisions` — two-unit collision decomposition (`.clu` + `.spk` → `.col`, `.eap`, `.tcl`)

## Synopsis

```bash
ndm_decomposecollisions [session.yaml] [--method M]
```

**Input:** per group `session.clu.<method>.N` (curated), `session.res.<method>.N`
and `session.spk.<method>.N` (resolved across methods); the session YAML

**Output:** per group `session.col.<method>.N` (see [`.col`](../formats/col.md));
for groups with at least one accepted decomposition, `session.eap.N`
([`.eap`](../formats/eap.md)) and `session.tcl.N` ([`.tcl`](../formats/tcl.md))
created or updated

## Description

When two neurons fire within about one spike width, their waveforms add and the
sum lands between the parent clusters. Run after sorting and curation, this step
finds such spikes and explains each as the sum of two unit templates. Only units
of the same spike group are combined. `.clu`, `.res` and `.spk` are never
modified.

The driver calls `process_decomposecollisions` (the CUDA build when it was
compiled, otherwise a link to `process_decomposecollisions_cpu`). For each
group:

1. **Templates.** Mean waveform of every unit with at least
   `minSpikesTemplate` spikes; clusters 0 and 1 are left out when
   `excludeNoise` is true. A group with fewer than two templates is skipped.
2. **Screening.** The noise RMS is estimated as median(|x|)/0.6745 over all
   waveform samples. Spikes whose peak-to-peak amplitude is below
   `minSnrRms` × RMS are not screened. For the others, the Pearson correlation
   with their own cluster's template (no shift, optionally limited to
   ±`corrWindow` samples around `peakSampleIndex`) is computed; a spike below
   `corrThreshold` is a collision candidate. Spikes of a cluster without a
   template are also candidates, but are recorded without a decomposition.
3. **Decomposition.** Component 1 is the spike's own cluster at zero shift,
   with a fitted amplitude. Component 2 is the best other template fitted to
   the residual over shifts of ±`maxShiftSamp` samples. Both get a parabolic
   sub-sample shift estimate.
4. **Acceptance.** A candidate is accepted when
   ‖residual‖ / ‖waveform‖ < `residualThreshold` and a second unit was found.

Every candidate, accepted or not, is a record in the `.col`.

Waveform geometry comes from the group's `spikeDetection.channelGroups` entry
(`nSamples`, default 52; `peakSampleIndex`, default 26; channel count). If the
resolved `.spk` is a stderiv file, the `.col` is flagged as such.

### File resolution

`method` selects the files: `.clu` and the `.col` output are strictly
`.<type>.<method>.N`. `.res` and `.spk` are looked up under the method, then
`standard`, then the untagged name, then any other method tag. A YAML parameter
file is required (exit 11 without one).

The step exits 11 when no group has a `.clu` or none has a `.spk`, and 10 when
every group that has both already has its `.col` (unless `overwrite` is
true). With `overwrite` false, individual groups whose `.col` exists are
skipped. If no group produces a `.col`, it exits 1.

### EAP template-class layer

Each accepted decomposition is also recorded in the method-less EAP layer: the
spike's row in `session.eap.N` gets a cell for each of the two units, holding
its integer shift (clamped to ±127). A unit is mapped to a class column through
`session.tcl.N`: the lowest active class whose `provenance_clu` equals the unit
ID is reused; otherwise a free class is allocated with that provenance, and the
pool grows by `nCells` columns (default 128) when no free class is left.
Amplitudes and fractional shifts stay in the `.col`.

If `session.eap.N` does not exist it is created all-absent with
`T = nCells` of the group (from the YAML, default 128); pre-build it with
[`process_initeap`](process_initeap.md) to set it up independently. If its spike
count differs from the group's, the `.eap` update is skipped with a message.
Groups with no accepted decomposition leave the layer untouched.

## Parameters

Set in the `ndm_decomposecollisions` block (see [parameters](../parameters.md)):

| Name | Default | Meaning |
|---|---|---|
| `maxShiftSamp` | `10` | Largest shift (± samples) tried for component 2; 10 samples ≈ ±0.31 ms at 32 kHz. |
| `corrThreshold` | `0.85` | Correlation with the own-cluster template below which a spike is a candidate. Raise (e.g. 0.92) for obvious collisions only, lower (0.75) for a wider net. |
| `residualThreshold` | `0.25` | Largest accepted ‖R‖/‖W‖; 0.25 means the two templates explain ≥ 75 % of the waveform norm. |
| `minSnrRms` | `4.0` | Minimum peak-to-peak amplitude, in units of the noise RMS, for a spike to be screened. |
| `minSpikesTemplate` | `30` | Minimum spikes for a unit to contribute a template. |
| `corrWindow` | `0` | Half-width (samples) around `peakSampleIndex` used for the screening correlation; 0 uses the whole waveform. |
| `excludeNoise` | `true` | Leave clusters 0 and 1 out of the templates. |
| `overwrite` | `false` | Recompute groups whose `.col` already exists. |
| `nWorkers` | `0` | CPU threads; 0 uses all cores. |
| `useGpu` | `false` | Use the CUDA path when the binary was built with CUDA. |
| `method` | `standard` | File variant (`standard`, `sdiff`, `stderiv`, `stderiv_S<n>`, `stderiv_C<n>`); `--method` overrides it. |

`process_decomposecollisions` also accepts `--clu-tag T` (read the curated stage
`session.clu.<method>.N.T`, write `session.col.<method>.N.T` and
`session.eap.N.T`), `--group G` (one group only) and `--n-samp N` (override
`nSamples`); the driver does not pass them.

## Viewing results

`src/ndmanager-plugins/scripts/collision_viewer.py SESSION [GROUP] [--method M]`
(not installed; run it from the source tree) browses the records of
`SESSION.col.<method>.N` with the raw waveform and the fitted components.

## See also

- [Collision decomposition workflow](../../workflows/collision-decomposition.md)
- [`.col`](../formats/col.md), [`.eap`](../formats/eap.md), [`.tcl`](../formats/tcl.md) — output formats
- [`process_initeap`](process_initeap.md) — pre-build the EAP layer
- [Algorithm design](../../design/decomposecollisions.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
