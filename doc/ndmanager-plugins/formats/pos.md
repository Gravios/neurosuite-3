# `.pos` — video tracking positions (text)

Plain ASCII, one position **sample per line**, whitespace-separated numeric
columns. For `M` tracked spots (LEDs/markers on the animal) each line holds
`2M` values — `x y` per spot — so a two-LED headstage gives four columns. `N`
lines = `N` samples, taken at the video/position sampling rate (a session
parameter, not stored in the file).

Read by **NeuroScope**, not by `neurofileio`: `PositionsProvider::loadData`
(`src/neuroscope/src/positionsprovider.cpp:48-124`) parses it, and the File ▸
**Load Position File** action routes through `NeuroscopeDoc::loadPositionFile`
(`neuroscopedoc.cpp:2055`). Produced by the video-tracking / acquisition system
(the recording rig's position tracker), not by an `ndm_*` plugin — so it is an
**adjacent** format (no `ndm_*` writer).

```
x1 y1 x2 y2
312 188 305 201
313 187 306 200
-1  -1  307 199
```

- **Columns** — `nbCoordinates` is taken from the **first line**'s token count,
  so any width (one `x y` pair per spot) is accepted.
- **Values** — parsed as decimal (scientific notation allowed) and **rounded to
  integer** pixel coordinates (`floor(0.5 + v)`).
- **Undetected spots** — by convention given **negative** coordinates; NeuroScope
  does not draw those samples (handbook §Position File).
- **Sampling** — one line per position sample; NeuroScope maps a time window to
  line indices via the position sampling rate. The open dialog filters
  `All Files (*.*)`, and the chosen file's extension gets its own stored
  sampling rate (defaulting to the session `videoSamplingRate`).
- **Display transforms** — `width` / `height` / `rotation` / `flip` (session
  parameters) are applied when drawing, not stored in the file.

For the user-facing description and the multi-spot layout, see the NeuroScope
handbook §Position File; the authoritative on-disk behavior is the
`PositionsProvider` reader cited above. `neurofileio` has no `.pos` path — this
is an adjacent format (see
[`../../../src/libneurosuite-core/docs/FILE_FORMATS.md`](../../../src/libneurosuite-core/docs/FILE_FORMATS.md) §4.7).

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
