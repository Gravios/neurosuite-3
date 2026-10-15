# `.pos` — video tracking positions (text)

Positions of tracked spots (LEDs or markers on the animal), one video sample per
line, for display in NeuroScope. No `ndm_*` step writes it: it comes from the
recording rig's tracker.

## Layout

Plain ASCII, whitespace-separated numbers, one sample per line, no header:

```
312 188 305 201
313 187 306 200
-1 -1 307 199
```

- **Columns** — `x y` per spot, so `M` spots give `2M` columns. The column count
  is taken from the **first line**, which is data: a header line would be read
  as a sample of zeros.
- **Values** — decimal, scientific notation allowed, rounded to integer pixel
  coordinates (`floor(0.5 + v)`).
- **Undetected spots** — negative coordinates; NeuroScope does not draw them.
- **Line count** — every line must have the same number of values. NeuroScope
  checks that the values read equal lines × columns and rejects the file
  otherwise. End the file with a newline (or the last value is not counted) and
  leave no blank lines (each counts as a sample).
- **Time** — line `i` is sample `i` at the video sampling rate, which is not
  stored in the file.

## Naming

`SESSION.pos`, or any name: NeuroScope's open dialog accepts any file. Not in
the custody type tables.

## Readers

- **NeuroScope** — File ▸ Load Position File (`NeuroscopeDoc::loadPositionFile`,
  parsed by `PositionsProvider::loadData` in `src/neuroscope/src/`). The
  sampling rate is stored per file extension and defaults to the session's
  `videoSamplingRate`. The session's video `width`, `height`, `rotation` and
  `flip` are applied when drawing.
- **`duration`** — reports a `.pos` file's length as lines / video frequency.
- **`ndm_clean`** — with `pos: true`, deletes `*pos` files other than
  `SESSION.pos`.

`neurofileio` has no `.pos` reader.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
