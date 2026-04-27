# `ndm_applydrift` — propagate curated drift to sibling shanks

Reads `SESSION.drift`, extracts the drift timeseries for a source
spike group, and writes adaptive chunk-boundary files (`SESSION.chunks.N`)
for each sibling group on the same probe. Each sibling's KlustaKwik run
can then be re-driven with chunk boundaries aligned to the real drift
trajectory rather than uniform time windows — dramatically improving
per-chunk cluster quality on chronic recordings with significant drift.

```bash
ndm_applydrift session.yaml 7           # source group 7; all siblings
ndm_applydrift session.yaml 7 3 5 9     # source group 7; siblings 3, 5, 9 only
```

With `-r` (re-run) passed on the command line, `ndm_klustakwik` is
immediately invoked for each target group after its `.chunks.N` is
written.


---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
