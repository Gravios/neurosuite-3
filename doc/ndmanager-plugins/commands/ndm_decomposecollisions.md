# `ndm_decomposecollisions` — collision decomposition (`.col.N`)

## Synopsis

```bash
ndm_decomposecollisions [session.yaml]
```

**Input:** `session.clu.<method>.N`, `session.res.<method>.N`, `session.spk.<method>.N`
**Output:**
- `session.col.N` per spike group (binary; see [`.col.N` format](../formats/col.md))
- the EAP template-class layer — [`session.eap.N`](../formats/eap.md) (membership
  + offset) and [`session.tcl.N`](../formats/tcl.md) (class registry) — one accepted
  decomposition per spike

The original `.clu`, `.res`, and `.spk` files are never modified.
Curated `.clu.N` files are required.

## Description

Identifies and decomposes spike waveforms representing two
near-simultaneous spikes. Never modifies the original `.clu.N` /
`.res.N` / `.spk.N` files. Requires curated `.clu.N` files.

**Algorithm:** (1) Build mean templates from curated clusters.
(2) Flag candidates with normalized cross-correlation below
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

**EAP template-class layer.** Besides the legacy `.col.N`, each accepted
decomposition is also recorded in the multi-label EAP layer: the colliding spike's
row in [`.eap.N`](../formats/eap.md) gets a present cell (with the fitted
integer-sample offset) for each constituent's class column, and
[`.tcl.N`](../formats/tcl.md) gains/reuses a class per colliding unit id (the
"same unit → same column" rule keyed on `provenance_clu`). The layer is created
all-absent if [`process_initeap`](process_initeap.md) has not pre-built it;
membership is stage-tagged (it follows the curated `.clu` tag) while the `.tcl`
registry is shared across stages. Groups with no accepted decomposition leave the
layer untouched.

## See also

- [Collision decomposition workflow](../../workflows/collision-decomposition.md)
- [`.col.N` format](../formats/col.md)
- [`.eap.N`](../formats/eap.md) / [`.tcl.N`](../formats/tcl.md) — the EAP template-class layer
- [`process_initeap`](process_initeap.md) — pre-construct the EAP layer
- [`../../design/decomposecollisions.md`](../../design/decomposecollisions.md) — algorithm design
- `collision_viewer.py` — bundled visualiser

---

*Part of the [ndmanager-plugins](../README.md) reference.*
