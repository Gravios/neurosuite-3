# `ndm_decomposecollisions` — collision decomposition (`.col.N`)

## Synopsis

```bash
ndm_decomposecollisions [session.yaml]
```

**Input:** `session.clu.N`, `session.res.N`, `session.spk.N` (or `.spkD.N`)
**Output:** `session.col.N` per spike group (binary; see [`.col.N` format](../formats/col.md))

The original `.clu`, `.res`, and `.spk` files are never modified.
Curated `.clu.N` files are required.

## Description

Identifies and decomposes spike waveforms representing two
near-simultaneous spikes. Never modifies the original `.clu.N` /
`.res.N` / `.spk.N` files. Requires curated `.clu.N` files.

**Algorithm:** (1) Build mean templates from curated clusters.
(2) Flag candidates with normalised cross-correlation below
`corrThreshold`. (3) Fit all same-shank pairwise template combinations
with shifts up to `maxShiftSamp`. (4) Accept when residual RMS fraction
< `residualThreshold`.

```yaml
- name: ndm_decomposecollisions
  parameters:
  - {name: maxShiftSamp,      value: 10,    status: Optional}
  - {name: corrThreshold,     value: 0.85,  status: Optional}
  - {name: residualThreshold, value: 0.25,  status: Optional}
  - {name: minSnrRms,         value: 4.0,   status: Optional}
  - {name: minSpikesTemplate, value: 30,    status: Optional}
  - {name: excludeNoise,      value: true,  status: Optional}
```

Results are visualised with `collision_viewer.py`.

## See also

- [Collision decomposition workflow](../../workflows/collision-decomposition.md)
- [`.col.N` format](../formats/col.md)
- [`../../design/decomposecollisions.md`](../../design/decomposecollisions.md) — algorithm design
- `collision_viewer.py` — bundled visualiser

---

*Part of the [ndmanager-plugins](../README.md) reference.*
