# `.res.<method>.N` — spike timestamps

Binary. No header. `nSpikes × int64_t`, little-endian. One sample index
per spike. File size = `nSpikes × 8` bytes.

`.res` is a **Shared** artifact under the
[variant naming convention](naming.md): spike times are method-independent,
so one physical copy is used across variants. It resolves by preferring
`<base>.res.<method>.N`, then `.res.standard.N`, then the untagged legacy
`.res.N`. Every `.clu` / `.clc` / `.spk` / `.fet` variant for a group is
aligned to the same `.res` spike order.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
