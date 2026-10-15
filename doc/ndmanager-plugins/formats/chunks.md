# `.chunks.N` — adaptive KiloKlustaKwik chunk boundaries

Written by [`ndm_applydrift`](../commands/ndm_applydrift.md) as
`SESSION.chunks.N`, one per spike group (method-less: the boundaries depend
on the drift trace, not on the feature space).  Read by `KlustaKwik` when
given `-ChunkFile SESSION.chunks.N`, in place of the uniform `ChunkMinutes`
grid.  [`ndm_klustakwik`](../commands/ndm_klustakwik.md) does not pass
`-ChunkFile`; `ndm_applydrift` does when `runKlustaKwik` is true.

## Layout

Plain text.  Lines starting with `#` (after optional whitespace) and empty
lines are comments.  Every other line is one boundary time in **seconds**
from the start of the recording, in ascending order.  `n` boundaries define
`n − 1` chunks; the first is `0` and the last is the end of the last drift
window.

```text
# KlustaKwik adaptive chunk boundaries
# session:      rat01
# group:        3
# source_group: 7  (drift reference shank)
# drift_file:   rat01.drift
# thresh_um:    5.0
# n_chunks:     3
# sampling_rate: 20000.0
#
# Pass to KlustaKwik via:  -ChunkFile rat01.chunks.3
# (overrides -ChunkMinutes for this group)
#
0.000000
1320.000000
2940.000000
3600.000000
```

KlustaKwik rejects a file whose boundaries decrease (error, exit 1), skips
lines that are not numbers with a warning, and ignores a file with fewer than
two boundaries (falling back to `ChunkMinutes`).

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
