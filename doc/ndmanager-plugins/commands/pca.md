# PCA feature extraction

Short utilities for pca feature extraction.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_pca` — raw-waveform PCA (`.spk` → `.fet`, `.pca`)

Computes principal component features from raw spike waveforms.
`process_pca` is OpenMP-parallelised across electrode groups.
Writes `.fet.N` (features, with timestamp column) and `.pca.N` (the
eigenvector basis used for projection — needed by klusters realignment
and by reextract workflows).

```bash
ndm_pca session.yaml       # all electrode groups (parallel)
ndm_pca session.yaml 3     # single electrode group
```


---

## Node parameters worth knowing

| Parameter | Default | Effect |
|---|---|---|
| `method` | `standard` | Chain-of-custody token; see [naming](../formats/naming.md#method-tokens). Accepts suffixed forms such as `stderiv_C5`. |
| `methodOrder` | 3 | Spatial-derivative order. **Ignored when the token carries its own order** — `stderiv_S3` *is* order 3. |
| `dropLastChannel` | `true` | See above. `false` keeps the dependent channel. |
| `varimax` | `true` | Rotate the retained components so each aligns with a spike-shape source rather than a variance-ordered mixture, which is what makes the feature axes legible for hand-sorting. `varimaxMaxIter` / `varimaxTol` tune it. |
| `extra` | — | Append per-channel peak values. Must match between `ndm_pca` and `ndm_refeaturize`. |

### Lagged feature space (`method: <token>_D<lag><dims>`)

A trailing `_D<lag><dims>` on the method — e.g. `stderiv_C5_D34` — makes
`ndm_pca` also write the lagged feature space fiber-kit uses (`fiber-session
--feat-lag`): per channel, PC1 at −lag, 0, +lag samples, plus PC2 at 0 when
`dims` is 4 (`dims` 3 = the three lags only).

| Reads | Writes |
|---|---|
| `.spk.stderiv_C5.N`, `.res.*.N` | `.fet/.pca.stderiv_C5.N` (ordinary, as before) |
| | `.fet/.pca.stderiv_C5_D34.N` (lagged, same fit) |

The lag basis is an ordinary PCAE file on the PCA window widened by `lag` on each
side (`recShift − lag`, `data2use + 2·lag`, `centered 0`, zero means), so the lag
`.fet` is exactly the projection of the `.spk` through it — Klusters,
`ndm_refeaturize` and fiber-kit consume it with no special case.  It has no peak
(`extra`) columns.  Column order per channel is `[PC1@−lag, PC1@0, PC1@+lag, PC2@0]`,
identical to `fiber_pca.lag_project`; on the reference chunk the `.fet` and `.pca`
match fiber-kit's `lag_basis`/`lag_project` applied to the same base basis exactly.
`before`/`after` must leave `lag` samples on each side of the window inside the
waveform, otherwise the run is refused rather than writing a non-lagged file under a
`_D` name.  To view the lag `.fet` in Klusters set the group's `nFeatures` to `dims`.
Existing outputs are never overwritten, and only the missing ones are written: with
`.fet.stderiv_C5.N` already present, a `stderiv_C5_D34` run refits, keeps the
stored ordinary `.fet`/`.pca`, and writes only the lag pair — warning if the refit
basis differs from the kept `.pca` (settings changed since it was written; the lag
space then follows the current settings).  The group is skipped only when both
`.fet` files exist.
No `.spk.<token>_D34` copy or link is needed: Klusters resolves the `.spk` of a `_D`
token to its waveform token's file (see [naming](../formats/naming.md#resolution-rules)).

```yaml
- name: ndm_pca
  parameters:
  - {name: method, value: stderiv_C5_D34, status: Optional}
  - {name: before, value: 12,             status: Optional}
  - {name: after,  value: 12,             status: Optional}
```

Differences from `fiber-session --emit-fet`: `ndm_pca` projects the `.spk` as
stored, while fiber-session realigns each spike first, and the lag basis records
the transform's input channel count like every `ndm_pca` basis.  Re-derive the lag
per session rather than copying 3.

`varimax` defaults **on**. A session whose node omits the key gets the rotation,
so re-running `ndm_pca` on an older session produces a *rotated* basis and
`.fet` — clusters will not sit where they did, and a `.clu` curated against the
old features refers to a feature space that no longer exists. Set
`varimax: false` for the unrotated variance-ordered basis.

## `ndm_pca_stderiv` — deprecated alias for `ndm_pca --method stderiv`

PCA in the stderiv method. Reads the shared raw `.spk`, applies the stderiv transform, passes the
waveforms through `process_pca_stderiv` (channel reduction for
rank-deficient orders 1 and 3), then through `process_pca` on the
reduced channel set. Writes `.fet.stderiv.N` and `.pca.stderiv.N`. Klusters and
KiloKlustaKwik auto-detect the D variant at open time.

`process_pca_stderiv` drops one channel before PCA for orders 1, 3, 4 and 5,
so `pca.nChannels = nElectrodes − 1`; orders 0 and 2 preserve full rank and
keep `pca.nChannels = nElectrodes`.

For orders 1 and 3 the dropped channel is an exact linear combination of the
others, so the drop costs nothing. For the custom-pattern orders 4 and 5 it
is a **convention** inherited from those orders, not a consequence: a custom
`sdiffPairs` pattern may well be full rank. And because PCA here is fit **per
channel** and truncated to `nFeatures`, even a rank-deficient transform does
not make the dropped channel's *features* exactly reproducible from the kept
ones. Set `dropLastChannel: false` on the `ndm_pca` node to keep every
channel (`-k` to `process_pca_stderiv`); `ndm_refeaturize` reads that value
from **`ndm_pca`'s** node, since it projects onto a basis `ndm_pca` fitted and
a width disagreement there is a silent mis-projection. Changing it
invalidates existing `.fet`/`.pca` — refit rather than mixing widths. Downstream consumers (shadowcluster,
klusters, KiloKlustaKwik) accept `pca.nChannels ≤ nChanGroup` and skip
the dropped channel automatically.



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
