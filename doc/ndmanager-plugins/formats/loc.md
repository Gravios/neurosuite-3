# `.loc.N` — per-spike source locations (binary float32)

Written by [`ndm_localise`](../commands/ndm_localise.md)
(`process_localise.py`), one file per spike group: `SESSION.loc.N`. The name
carries no method tag.

## Layout

Little-endian `float32`, no header, five values per spike, one row per spike
of the group's `.spk` in the same order:

| Column | Unit | Meaning |
|---|---|---|
| `x_um` | µm | Lateral source position in the probe plane |
| `y_um` | µm | Source height above the shank tip |
| `z_um` | µm | Distance from the shank face, in [1, `maxDistUm`] |
| `amplitude` | ADC counts × µm | Fitted monopole amplitude *A* |
| `residual_rms` | log amplitude | RMS of the log-space fit residual; 0 = perfect |

```python
loc = np.fromfile("session.loc.3", dtype="<f4").reshape(-1, 5)
```

Special values:

- **Failed fit** (optimiser failure, or source farther than `maxDistUm` from
  every site): `x_um`, `y_um`, `z_um` and `residual_rms` are NaN,
  `amplitude` is 0.
- **Clusters 0 and 1** (from the `.clu`): position and amplitude are fitted,
  `residual_rms` is NaN.
- **`method: com`**: `z_um` and `residual_rms` are NaN; `amplitude` is the sum
  of the per-channel peak-to-peak amplitudes.

## `SESSION.loc.yaml`

A YAML sidecar shared by all groups; each run replaces its own group's entry
and keeps the others:

```yaml
localisation:
  format: '1.0'
  method: monopole
  session: session
  columns: [x_um, y_um, z_um, amplitude, residual_rms]
  dtype: float32-little-endian
  notes: ...
  groups:
  - group: 3
    n_spikes: 18234
    n_sites: 8
    n_localised: 16890
    n_failed: 12
    n_noise: 1332
    probe_file: yaml-inline        # or the probe file, or default-linear-50um
    site_positions_um: [[0.0, 140.0], ...]
    output_file: session.loc.3
    stats:
      x_um: {mean: ..., std: ...}
      y_um: {mean: ..., std: ...}
      z_um: {mean: ..., std: ...}
```

`method` is the one of the run that created the file. `stats` cover spikes of
clusters ≥ 2 with a successful fit.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
