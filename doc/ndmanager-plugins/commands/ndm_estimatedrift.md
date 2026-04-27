# `ndm_estimatedrift` — spatial probe drift estimation (`.drift`)

## Synopsis

```bash
ndm_estimatedrift [session.yaml]
```

**Input:** `session.res.N`, `session.clu.N`, optionally `session.spk.N`; per-group geometry from session YAML (`probeId`, `shankIndex`, `sitePositions_um`)
**Output:** `session.drift` (YAML; see [`.drift` format](../formats/drift.md))

## Description

Estimates in-vivo probe drift from curated spike-sorting results.
Requires `.res.N`, `.clu.N`, and optionally `.spk.N`. Reads `probeId` /
`shankIndex` / `sitePositions_um` from the YAML (written by
`ndm_setupgroups` or `ndm_aom2dat`) to reconstruct site geometry.

**Primary method:** per-unit amplitude-vs-depth fingerprint
cross-correlation with sub-site parabolic interpolation.
Weighted-median drift estimate per shank.
**Fallback:** population-level spatial fingerprint cross-correlation
when unit yield is low.

```yaml
- name: ndm_estimatedrift
  parameters:
  - {name: windowSec,    value: 60,   status: Optional}
  - {name: minUnits,     value: 3,    status: Optional}
  - {name: minSpikes,    value: 20,   status: Optional}
  - {name: excludeNoise, value: true, status: Optional}
```

Accepts both the binary and legacy text `.clu` formats. `n_samp` is
read from YAML per group (one value per electrode group, passed via
`--n-samples-per-group`).

## See also

- [`ndm_applydrift`](ndm_applydrift.md) — propagate the trajectory to sibling shanks
- [Drift correction workflow](../../workflows/drift-correction.md)
- [`.drift` format](../formats/drift.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
