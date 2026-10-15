# `.evt` — timestamped events (text)

Labelled events for display in NeuroScope: stimuli, digital inputs, behavioural
marks, gaps in a recording.

## Layout

Plain text, one event per line, in ascending time order:

```
0.05	start
12345.6	stim_on
12845.6	stim_off
```

- **Time** — the first whitespace-delimited token, a decimal number of
  **milliseconds** from sample 0 of the recording (not a sample index;
  fractional values allowed).
- **Label** — the rest of the line after the time and its following
  whitespace; it may contain spaces. Writers separate the two with a TAB.
- Blank lines and lines that do not start with a number are skipped.
- NeuroScope takes the time on the last line as the end of the file, so keep the
  lines sorted.
- Write times with enough digits: NeuroScope saves 12 significant digits.

Reader: `neurofileio::readEvt` (`src/libneurosuite-core/src/neurosuite/core/neurofileio.cpp`).

## Naming

NeuroScope requires `<base>.<id>.evt` or `<base>.evt.<id>`, where `<id>` is a
three-character identifier (used as the event file's name in the display).
`.evt` is not in the custody type tables ([naming](naming.md)).

## Writers

| Writer | File |
|---|---|
| [`ndm_plx2dat`](../commands/ndm_plx2dat.md) (`process_plxconvert`) | `SESSION.<id>.evt` (default id `plx`): every Plexon digital event, ms from `.dat` sample 0, plus `gap start` / `gap end` around zero-filled pauses |
| [`ndm_nev2evt`](../commands/format-conversion.md) (`process_nlxconvert`) | `SESSION-<suffix>.nlx.evt` from Neuralynx `.nev` |
| [`ndm_smr2evt`](../commands/format-conversion.md) (`process_smrconvert -e`) | one file per event channel, `<base>.e<NN>.evt` (channel number padded to three characters: `.e05.evt`, `.e12.evt`, `.123.evt`), from CED `.smr` |
| NeuroScope | the event file being edited, on Save |

## Readers

NeuroScope (`EventsProvider`), via `neurofileio::readEvt`.

## See also

- [Format conversion](../commands/format-conversion.md)
- [`.dat`](dat.md) — the recording the times refer to.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
