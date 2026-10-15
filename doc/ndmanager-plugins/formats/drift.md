# `.drift` — probe drift trajectories (YAML)

Written by [`ndm_estimatedrift`](../commands/ndm_estimatedrift.md) as
`SESSION.drift`.  Per probe and per shank (spike group), the drift in µm of
each time window relative to the shank's reference window.  Read by
[`ndm_applydrift`](../commands/ndm_applydrift.md).  The same run writes the
binary per-sample signal [`SESSION.dat.drift.P`](#binary-drift-signal-sessiondatdriftp).

Not to be confused with `SESSION.drift.N.yaml` (the per-group report of
[`ndm_drifttracker`](../commands/ndm_drifttracker.md)).

## Layout

```yaml
drift:
  format: '1.0'
  methods: [per_unit_xcorr, xcorr_population]
  primaryMethod: per_unit_xcorr
  windowSec: 60.0
  minUnits: 3
  minSpikes: 20
  outlierThreshold: 5.0
  weightMode: geometry
  probes:
  - probeId: 0
    label: ''
    drift:                          # probe-level average of the estimated shanks
      method: weighted-mean-across-shanks
      weightField: nUnitsInRef
      sourceGroups: [1]
      sourceWeights: [6]
      nShanks: 1
      maxAbsDrift_um: 3.4
      windows:
      - {t_start: 0.0,  t_end: 60.0,  drift_um: 0.0,  n_contributing_shanks: 1, stddev_um: 0.0}
      - {t_start: 60.0, t_end: 120.0, drift_um: -1.9, n_contributing_shanks: 1, stddev_um: 0.0}
    shanks:
    - shankIndex: 0
      spikeGroup: 1
      nUnitsQualified: 8
      nUnitsInRef: 6
      refWindowIndex: 0
      refWindowT_start: 0.0
      maxAbsDrift_um: 3.4
      driftRate_um_per_hr: 2.1
      windows:                      # window 0 (reference, drift_um 0.0) omitted
      - t_start: 60.0
        t_end: 120.0
        drift_um: -1.9
        per_unit_xcorr:
          drift_um: -2.0
          n_inlier_units: 5
          units:
          - {unit: 2, ref_com_um: 82.4, win_com_um: 80.9, xcorr_shift_um: -2.1,
             com_delta_um: -1.5, weight: 0.84, weight_sensitivity: 0.0213,
             weight_curvature: 0.41, weight_n_active: 4.0, weight_n_spikes: 31.6,
             outlier: false}
        xcorr_population: {drift_um: -1.6, peak_curvature: 0.37}
    - shankIndex: 1
      spikeGroup: 2
      skipReason: noise-only-clu
      skipDetail: all spikes assigned to clusters [0, 1] (noise / artefact); no real units to track
      nSpikes: 41250
      nClusters: 2
      windows: []
```

### Top level

The run's settings: `windowSec`, `minUnits`, `minSpikes`,
`outlierThreshold` (µm), `weightMode`, and the two methods run.

### Probe entries

| Key | Meaning |
|---|---|
| `probeId` | The probe's `probeId` in the session YAML (0 when the session has no `probes:` list). |
| `label` | The probe entry's `label`, if any. |
| `drift` | Probe-level trace: per window, the mean of the estimated shanks' `drift_um` weighted by their `nUnitsInRef`, with the across-shank standard deviation. Present when at least one shank on the probe was estimated. `ndm_applydrift` prefers it. |
| `shanks` | One entry per spike group, ordered by `shankIndex`. |

### Shank entries

| Key | Meaning |
|---|---|
| `shankIndex`, `spikeGroup` | Shank on the probe; 1-based spike group. |
| `nUnitsQualified` | Units with at least `minSpikes` spikes over the session. |
| `nUnitsInRef`, `refWindowIndex`, `refWindowT_start` | Units in the reference window (the first window with `minUnits` active units), its index and start time (s). |
| `maxAbsDrift_um`, `driftRate_um_per_hr` | Largest absolute window drift; range of window drifts divided by the recording duration in hours. |
| `windows` | Per window: `t_start`, `t_end` (s), `drift_um` (the combined estimate, `null` if none), `per_unit_xcorr` (estimate, number of inlier units, and per-unit detail with weight components and outlier flag) and `xcorr_population` (estimate and peak curvature, 0–1). |
| `skipReason`, `skipDetail` | Present instead of a trajectory when the group could not be estimated: `noise-only-clu` (only clusters 0/1) or `insufficient-units-per-window` (no window reached `minUnits`). `windows` is then empty. |
| `derivedFrom` | Set on entries copied from another group when the helper is run with `--source-group` (not used by the `ndm_estimatedrift` wrapper). |

Depths are heights along the shank taken from the probe file by channel
number (see [`.probe`](probe.md#geometry-and-channels)); the profiles are
sorted by depth before cross-correlation, so the sign of `drift_um` does not
depend on the order in which a group lists its channels.

## Binary drift signal (`SESSION.dat.drift.P`)

One file per probe, written only when `SESSION.dat` exists.  `P` is the
probe's 1-based position in the session YAML's `probes:` list (`probeId + 1`
if the probe is not listed).

- Headerless, little-endian `int16`, one value per recording sample (the
  sample count of `SESSION.dat`), in µm relative to the reference window,
  rounded to the nearest µm.
- Built from the first shank of the probe (lowest `shankIndex`) that has
  windows — not from the probe-level average — by linear interpolation
  between window midpoints, held constant before the first and after the
  last midpoint.  Windows with `drift_um: null` are skipped.
- No file is written for a probe with no estimated shank.

Read by [`ndm_driftcorrect`](../commands/ndm_driftcorrect.md).

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
