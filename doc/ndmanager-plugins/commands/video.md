# Video processing

Short utilities for video processing.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_extractleds`

Detects bright pixels per video frame by thresholding the luma channel.
Requires FFmpeg dev headers at build time.


---

## `ndm_transcodevideo`

Converts video to MPEG-1 or H.264 using FFmpeg.



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*
