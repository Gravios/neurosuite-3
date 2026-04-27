# `ndm_localise` — per-spike source localisation (`.loc.N`)

Fits a monopole (point-source) extracellular potential model per spike:

```
V_i(x_s, y_s, z_s, A) = A / sqrt((x_i - x_s)² + (y_i - y_s)² + z_s²)
```

where `(x_i, y_i)` are the known electrode site positions (from the
YAML `sitePositions_um` inline or from the probe library) and
`(x_s, y_s)` is the source position in the plane of the shank; `z_s`
is the perpendicular distance from the shank face. `A` is an unsigned
amplitude. Writes `.loc.N` with one row per spike
(`x_s, y_s, z_s, A`, + fit residual).


---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
