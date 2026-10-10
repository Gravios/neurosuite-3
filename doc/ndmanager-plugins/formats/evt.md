# `.evt` — timestamped events (text)

Plain text, one event per line: a millisecond timestamp and a label. The
authoritative reader/writer is `neurofileio::readEvt` / `writeEvt` (see
[`../../../src/libneurosuite-core/docs/FILE_FORMATS.md`](../../../src/libneurosuite-core/docs/FILE_FORMATS.md) §3.5);
NeuroScope loads and displays these, and several `ndm_*` command tools read and
write them.

```
0.050000	start
12345.600000	stim_on
12845.600000	stim_off
```

- Each line is `"<time_ms>\t<label>"`.
- **time** is a **`double` in milliseconds** (not a sample index) — the first
  whitespace-delimited token on the line.
- **label** is the **remainder of the line** after that token, so a label may
  contain spaces.
- `writeEvt` emits `time_ms '\t' label '\n'`.

By NeuroScope convention an event file often carries a three-letter type code
in its extension (e.g. `base.evt.rip`, `base.evt.pos`); the line format above is
the same regardless of the code.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
