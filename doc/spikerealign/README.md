# spikerealign — Spike Waveform Realignment

Spike realignment aligns each spike's waveform to its cluster's mean template
by normalized circular cross-correlation, correcting jitter in
threshold-crossing detection. It is used interactively in Klusters and by
KiloKlustaKwik as Phase 1.5 of chunked CEM sorting.

In Klusters it is **Actions → Realign Spikes…** (`Shift+L`), which realigns
the first selected cluster of the active display, and **Actions →
PCA-Center Align All Clusters** (`Shift+P`), which runs over every cluster
except 0 and 1.

There is no standalone binary and no single realignment class. The pieces are:

- the cross-correlation kernel `realign_xcorr` with its backend dispatcher
  (namespace `XcorrDispatch`, `realign_xcorr_dispatch.cpp`) in
  `src/libklustersshared/src/xcorr/`;
- the Klusters document side, `KlustersDoc` in
  `src/klusters/src/klustersdoc_realign.cpp`;
- `RealignWorker` (`src/klusters/src/realignworker.*`), which runs it on a
  background thread.

---

## Algorithm

The realignment runs once per cluster:

1. Read all waveforms for the cluster from the `.spk` file.
2. Compute the cluster mean template (per sample, per channel). Pre-shift the template so its amplitude peak (summed across all channels) lands at the expected peak position. Without this step the template is misaligned and the cross-correlation would shift every spike *away* from the true peak.
3. Pack all waveforms and the template into channel-major buffers and compute the optimal shift for every spike simultaneously using normalized circular cross-correlation summed across **all channels** via `XcorrDispatch` (CUDA → HIP → SYCL → OpenMP, depending on what was built and is available; SYCL is used only when `KLUSTERS_USE_SYCL=1` is set).
4. For each spike whose score passes the threshold and whose optimal shift is non-zero:
   - (a) Move the timestamp by the full shift and update the `.res`.
   - (b) Re-extract the shifted waveform from the `.fil` (or `.dat`) at the new timestamp.
   - (c) Write the new waveform to the `.spk` in the same row.
   - (d) Re-project it through the PCA basis of the session method (`.pca.<method>.N`) and update the `.fet.<method>.N` row.
   - (e) Rows are never reordered, so every method's files and the `.clu` stay aligned. A spike's new time may pass a neighbouring spike's; **Actions → Sort Spikes by Time** puts the rows back in time order.

Sign convention (consistent with `realign_xcorr.h`): a positive shift `τ` means the spike peak is *late* by `τ` samples relative to the template. Correcting by `newTimestamp = oldTimestamp + τ` moves the spike earlier in the waveform window so its peak aligns with the template.

---

## Files read and written

| File | Access | Description |
|---|---|---|
| `base.yaml` | read | Session parameters (channels, waveform length, peak sample) |
| `base.spk.<method>.N` | read/write | Waveforms — re-extracted spikes replace their rows |
| `base.res.N` | read/write | Timestamps |
| `base.clu.<method>.N` | read | Cluster IDs — never reordered |
| `base.fet.<method>.N` | read/write | Features — re-projected rows |
| `base.pca.<method>.N` | read | PCA basis used for the re-projection |
| `base.fil` / `base.dat` | read | Signal the waveforms are re-extracted from |

All writes go to working copies (`<file>.pending`) and reach the session
files only on **Save**. File naming: [variant naming](../ndmanager-plugins/formats/naming.md).

---

## Integration with klusters

A realignment is kept as soon as it finishes; there is no review dialog. The
views switch to the Overview Display with the realigned cluster selected, and
the change is written by the next Save.
To discard it, use **Undo** (`Ctrl+Z`) or close without saving.

The settings are in **Settings → Preferences → Refinement → Realignment
(built-in xcorr)**: threshold (minimum score), iterations, maximum shift
(0 = automatic) and an optional post-alignment pass. See
[Spike realignment](../klusters/README.md#spike-realignment) in the Klusters
manual.

---

## KiloKlustaKwik Phase 1.5

KiloKlustaKwik's chunked CEM mode also performs waveform realignment (Phase 1.5) directly on the `.spk` file after per-chunk sorting, before the global merge step. It uses the same `XcorrDispatch` kernel but operates on all clusters in all chunks in a single batch pass. See `src/kiloklustakwik/CHANGES.md` for details.

---

## Installation

The realignment code is compiled into Klusters and KiloKlustaKwik; there is
nothing separate to install. See [Installing neurosuite-3](../INSTALL.md).
