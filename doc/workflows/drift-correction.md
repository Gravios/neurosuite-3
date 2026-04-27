# Drift correction

Estimates probe drift from one carefully curated shank, then
propagates the drift trajectory to sibling shanks on the same probe.
Use this for chronic recordings spanning hours where electrode drift
is visible as cluster centres shifting over time.

The two-step structure (estimate from one shank, apply to all) means
you only have to fully curate one shank's drift — the others inherit
its trajectory via adaptive chunk boundaries.

## Steps

### 1. Pick an anchor shank

Choose a shank where you have:

- A clean curated `.clu.N` from a [first-time sort](first-time-sort.md).
- At least two well-isolated, high-firing-rate clusters spanning the
  recording duration.
- Visible drift — clusters whose centroid in feature space moves over
  time.

Drift estimation uses cross-correlation of feature distributions
across time. Without high-firing units anchoring the estimate, the
result is noisy and unreliable.

### 2. Estimate drift on the anchor shank

```sh
ndm_estimatedrift session.yaml --group N
```

Outputs `session.drift` — a YAML with the per-time-bin drift
trajectory. Sub-pixel parabolic interpolation is applied when
`scipy` is available (recommended).

For algorithm details and parameter knobs, see
[`ndm_estimatedrift`](../ndmanager-plugins/commands/ndm_estimatedrift.md).

### 3. Inspect the drift trajectory

The `session.drift` YAML can be viewed directly. Look for:

- **Smooth monotonic trends** — typical of mechanical drift; safe to
  apply.
- **Sudden jumps** — usually electrode resets or recording artefacts;
  may indicate a bad time interval that should be excluded.
- **High-frequency oscillation** — algorithm noise; suggests the
  anchor shank's clusters aren't isolated enough or firing rates are
  too low.

If the trajectory looks unreliable, pick a different anchor shank or
consult [`../design/optimization.md`](../design/optimization.md) for
recipes that improve estimation.

### 4. Propagate to sibling shanks

```sh
ndm_applydrift session.yaml
```

Uses the anchor's drift to compute *adaptive chunk boundaries* for
every other shank — a `.chunks.N` file per group. KlustaKwik's
chunked-CEM pipeline reads `.chunks.N` and uses the suggested
boundaries instead of fixed time chunks, so each chunk corresponds
to a stable feature-space regime rather than a fixed wall-clock
window.

For details on adaptive chunks vs fixed chunks, see
[`../klustakwik/README.md`](../klustakwik/README.md) and
[`ndm_applydrift`](../ndmanager-plugins/commands/ndm_applydrift.md).

### 5. Re-sort the affected shanks

```sh
ndm_klustakwik session.yaml
```

KlustaKwik picks up the new `.chunks.N` files automatically.
Clusters that previously appeared smeared across chunk boundaries
should now be tighter.

### 6. Curate

Open each affected shank in Klusters and curate as usual. Drift-
corrected sorts often reveal pairs of units that previously masked
each other because they drifted at different rates — these typically
need split-or-merge decisions during curation.

## Limitations

- Drift correction handles **continuous probe drift**. It does *not*
  handle electrode dropouts, sudden gain changes, or stage bumps —
  those need to be cut out of the recording before sorting.
- The anchor shank's drift is assumed representative of the whole
  probe. For probes whose shanks drift independently (rare but
  possible with very long shanks or heat-related expansion), each
  shank needs its own anchor. The toolchain doesn't currently
  automate this case.
- Drift correction is *post-hoc* — the original `.dat` and `.fil`
  aren't modified. KlustaKwik gets time-varying chunk boundaries; the
  signal trace is unchanged. This is intentional, but means
  downstream waveform analyses still see the original (drifted)
  waveforms.

## See also

- [`ndm_estimatedrift`](../ndmanager-plugins/commands/ndm_estimatedrift.md)
- [`ndm_applydrift`](../ndmanager-plugins/commands/ndm_applydrift.md)
- [`.drift` format](../ndmanager-plugins/formats/drift.md)
- [`.chunks.N` format](../ndmanager-plugins/formats/chunks.md)
