# `.chunks.N` — adaptive KlustaKwik chunk boundaries

Written by `ndm_applydrift`. One chunk per line:
`start_sample end_sample`. KlustaKwik reads this file (when the
`-ChunkBoundaries` flag is passed) in place of computing uniform chunks
from `ChunkMinutes`.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
