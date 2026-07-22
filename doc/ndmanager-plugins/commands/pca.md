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
