# `.drift` — probe drift trajectories (YAML)

Written by `ndm_estimatedrift`. Per-probe, per-shank drift in µm per
time window. Consumed by `ndm_applydrift`.

```yaml
drift:
  format: '1.0'
  method: unit_com
  windowSec: 60.0
  probes:
    - probeId: 0
      shanks:
        - shankIndex: 0
          spikeGroup: 1
          nUnitsTotal: 8
          windows:
            - {t_start: 0.0,  t_end: 60.0,  drift_um: 0.0}
            - {t_start: 60.0, t_end: 120.0, drift_um: -1.8}
```


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
