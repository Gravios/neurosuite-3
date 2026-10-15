# Video processing

LED position extraction and video transcoding. Both scripts take the
session YAML (or nothing, to use the current directory name) as their
only argument and read their parameters from the session YAML (see
[parameters](../parameters.md)). Video files are looked up in
`../DIR-SUFFIX/`, as described under
[raw-data layout](format-conversion.md#raw-data-layout).

**Contents:** [`ndm_extractleds`](#ndm_extractleds--led-positions-from-video-or-tracker-data) · [`ndm_transcodevideo`](#ndm_transcodevideo--re-encode-the-session-video)

---

## `ndm_extractleds` — LED positions from video or tracker data

### Synopsis

```bash
ndm_extractleds [session.yaml]
```

**Input:** a Neuralynx `.nvt` tracker file, or a video file `SESSION.EXT`, `../DIR-SUFFIX/SESSION-SUFFIX.EXT` or `../DIR-SUFFIX/SESSION-SUFFIX-<n>.EXT`

**Output:** `.spots` (one per video file, named after it) and, when the recording system provides them, `SESSION.sts` frame timestamps

### Description

Skipped when `SESSION.spots` exists (exit 10).

**`useTrackerData: true`** — if a `.nvt` file exists in `../DIR-SUFFIX/`,
runs [`ndm_nvt2spots`](format-conversion.md#ndm_nvt2spots--neuralynx-nvt--spots--sts),
which reads its own `ndm_nvt2spots` parameter block and writes
`SESSION.spots` and `SESSION.sts`. Without a `.nvt` file the step is
skipped (exit 11).

**`useTrackerData: false`** — finds the video files, trying in order
`SESSION.EXT` in the current directory, `../DIR-SUFFIX/SESSION-SUFFIX.EXT`,
then the numbered parts `../DIR-SUFFIX/SESSION-SUFFIX-<n>.EXT`, and runs
`process_extractleds -t <threshold>` on each. Every frame is decoded with
FFmpeg (any container and codec it supports), pixels with luminance above
`threshold` are grouped into connected spots, and each spot is written as
one line of the `.spots` file in the current directory, named after the
video (`SESSION-SUFFIX.spots`, …):

```
frame  nPixels  meanX  meanY  sdX  sdY  meanLuma  meanCr  meanCb
```

Then, if the raw directory holds `.smi` files, it runs
[`ndm_smi2sts`](format-conversion.md#ndm_smi2sts--neuralynx-smi--sts);
if it holds a `.tsp` file, [`ndm_tsp2sts`](format-conversion.md#ndm_tsp2sts--amplipex-tsp--sts).
Both write `SESSION.sts`.

`process_extractleds` also has automatic (Otsu) thresholding, spot-size
limits (`-minsize`, `-maxsize`), LED colour separation (`-colors`,
`-color`) and position smoothing (`-smooth`); the script does not expose
them, so run the binary directly to use them. It is only built when the
FFmpeg development libraries are found.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffix` | — | Suffix of the directory holding the tracker or video files. |
| `useTrackerData` | `true` (template) | Mandatory. `true`: positions from the Neuralynx `.nvt` tracker file; `false`: LED detection on the video. |
| `threshold` | `90` (template) | Luminance threshold, 0–255. Required when `useTrackerData` is `false`. |
| `extension` | `avi` (template) | Video file extension (`avi`, `mpg`, …). Required when `useTrackerData` is `false`. |
| `show` | `false` (template) | No effect: the video is never displayed. Kept for old parameter files. |

```yaml
- name: ndm_extractleds
  parameters:
    - name: suffix
      status: Mandatory
    - name: useTrackerData
      value: 'true'
      status: Mandatory
    - name: threshold
      value: 90
      status: Mandatory
    - name: extension
      value: avi
      status: Mandatory
    - name: show
      value: 'false'
      status: Mandatory
```

---

## `ndm_transcodevideo` — re-encode the session video

### Synopsis

```bash
ndm_transcodevideo [session.yaml]
```

**Input:** `../DIR-SUFFIX/SESSION-SUFFIX.EXT`

**Output:** `SESSION.m1v` (`codec: m1v`) or `SESSION.avi` (`codec: x264`)

### Description

Re-encodes the session video with `avconv` (libav); `avconv` and
`avprobe` must be installed. If `codec` is unset, nothing is done
(exit 0). A missing input skips the step (exit 11).

| `codec` | Output | Encoding |
|---|---|---|
| `m1v` | `SESSION.m1v` | MPEG-1 elementary video stream, 800 kbit/s |
| `x264` | `SESSION.avi` | H.264 (libx264), constant quantiser 20 |

Skipped (exit 10) when the output exists and `avprobe` reports the target
codec; an existing file with another codec (e.g. the original `.avi`) is
overwritten. Any other `codec` value does nothing.

### Parameters

| Name | Default | Meaning |
|---|---|---|
| `suffix` | — | Mandatory. Suffix of the directory holding the video. |
| `extension` | `avi` (template) | Mandatory. Extension of the original video. |
| `codec` | — | `m1v` or `x264`; leave empty for no transcoding. |
| `frequency` | 30 | Optional. Output frame rate (frames/s). |

```yaml
- name: ndm_transcodevideo
  parameters:
    - name: suffix
      status: Mandatory
    - name: extension
      value: avi
      status: Mandatory
    - name: codec
      status: Mandatory
```

---

## See also

- [Format conversion](format-conversion.md) — `ndm_nvt2spots`, `ndm_smi2sts`, `ndm_tsp2sts`
- [`ndm_concatenate`](channel-manipulation.md#ndm_concatenate--concatenate-the-sessions-of-a-directory) — checks `.spots` duration against the wideband data
- [`.pos` format](../formats/pos.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
