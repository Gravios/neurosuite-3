# ndm_alignspikes .res-update fix — usage & recovery

## What changed

`process_alignspikes` now updates `.res.N` so `.res[i] = originalTs[i] + shift[i]`
after alignment, restoring the invariant that Klusters' nudge (and any other
tool re-reading `.fil` at `.res` offsets) requires:

```
.spk[i] peak  ≡  .fil at file-sample .res[i]
```

Before this fix, alignspikes wrote new `.spk.N` content centred on the true
peak but left `.res.N` unchanged (still pointing at the detection
threshold-crossing). Any tool that subsequently re-extracted from `.fil`
at the `.res` position would land `shift[i]` samples off, producing
window-position errors of typically 1–3 samples — the cause of the nudge
"shifts too far" bug observed in cluster 3 etc.

The pre-alignment `.res.N` is archived to `.res.N.prealign` on first run,
so the original detection timestamps can be recovered.

## Recovery for the existing session

The data already on disk for jg05-20120316 was aligned under the old
behaviour, so `.res` and `.spk` are out of sync.  To fix:

```bash
# 1. Restore an unaligned baseline.  Use canonical extractspikes output
#    if you still have it; otherwise re-run extraction:
ndm_extractspikes_stderiv jg05-20120316     # or ndm_extractspikes for raw

# 2. Re-run alignspikes with the patched binary (it'll archive .res to
#    .res.N.prealign and write the synced .res):
ndm_alignspikes jg05-20120316

# 3. Re-run PCA so .fet / .pca match the realigned .spk:
ndm_pca_stderiv jg05-20120316              # or ndm_pca for raw pipeline

# 4. (Re-cluster as needed.)
```

If you'd rather not re-run the whole pipeline, the alternative is to
rebuild `.res.N` from the existing `.spk.N` content directly — find the
position of max |stderiv| amplitude per spike in `.spk`, add the spike
window offset, and write that to `.res`.  Tedious but possible; only
worth it if re-running detection isn't an option.

## Verifying the fix worked

After re-running alignspikes with the patched binary:

```bash
# Diff the new .res against .res.prealign — should differ by per-spike
# integer shifts in [-maxShift, +maxShift]:
python3 - <<'PY'
import numpy as np
new = np.fromfile('jg05-20120316.res.6',          dtype=np.int64)
old = np.fromfile('jg05-20120316.res.6.prealign', dtype=np.int64)
d   = new - old
print('shifts: min={}, max={}, mean_abs={:.2f}, n_nonzero={} of {}'.format(
    d.min(), d.max(), np.abs(d).mean(), int((d != 0).sum()), len(d)))
PY
```

`d` should be a vector of small integers, mostly within `[-maxShift, +maxShift]`.

Then in Klusters, run a +1 / -1 nudge on a clean cluster with
`NUDGE_DUMP_MEAN=1` set, and inspect the resulting dump — the after-trough
should land exactly 1 sample later than the before-trough.

## Pipeline-position note

The fix doesn't change when alignspikes runs in the pipeline.  If you've
already run alignspikes and haven't yet run ndm_pca_stderiv, the
`.fet/.pca` files are still stale — but they were stale before too
(alignspikes renames them to `.prealign`).  The caller must re-run PCA
either way.
