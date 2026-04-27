# `ndm_redetectspikes` — second-round detection on cleaned `.dat`

## Synopsis

```bash
ndm_redetectspikes [session.yaml]
```

**Input:** `session-spkclean.dat` (produced by `ndm_stripdat`)
**Output:** updated `session.res.N`, `session.spk.N`, `session.clu.N`; `.fet.N` is deleted (spike order changed)

## Description

After `ndm_stripdat`, runs the full spike detection pipeline
(`ndm_hipass` → `ndm_extractspikes`) on `SESSION-spkclean.dat` to
recover spikes that were missed in the first pass. Merges newly
detected spikes into the existing `.res` / `.spk` / `.clu` files via
`process_mergespikes`, then deletes `.fet.N` (spike order changed).
Re-run `ndm_pca` and `ndm_klustakwik` to rebuild features and re-sort
the merged set.

**Iterative refinement workflow:**

```bash
ndm_klustakwik session.yaml     # initial sort
# (klusters — curate pass 1)
ndm_stripdat session.yaml       # subtract pass-1 clusters
ndm_redetectspikes session.yaml # detect missed spikes
ndm_pca session.yaml            # rebuild features
ndm_klustakwik session.yaml     # re-sort merged set
# (klusters — curate pass 2)
ndm_lfp session.yaml            # LFP from improved cleaned dat
```

`ndm_reextractspikes` is an alternative, less destructive path — it
appends new spikes to the existing `.res.N` in chronological order
rather than overwriting the feature file.

## See also

- [`ndm_stripdat`](ndm_stripdat.md) — produces the cleaned `.dat`
- [`ndm_reextractspikes`](ndm_reextractspikes.md) — alternative, less destructive path
- [Iterative refinement workflow](../../workflows/iterative-refinement.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
