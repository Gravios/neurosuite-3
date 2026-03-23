# spikerealign — Spike Waveform Realignment

SpikeRealign is the waveform realignment engine used by the klusters interactive GUI. It aligns spike waveforms to the cluster mean template via normalised circular cross-correlation, correcting for jitter in threshold-crossing detection.

Realignment is available interactively from within klusters (cluster-by-cluster from the GUI via **Realign → Realign selected cluster**) and is also invoked automatically by KlustaKwik as Phase 1.5 of chunked CEM sorting.

SpikeRealign is **not** a standalone binary. It is a C++ class (`SpikeRealign`) called by `RealignWorker` inside the klusters process.

---

## Algorithm

The realignment runs once per cluster:

1. Read all waveforms for the selected cluster from the binary `.spk` file.
2. Compute the cluster mean template (per sample, per channel). Pre-shift the template so its amplitude peak (summed across all channels) lands at the expected peak position. Without this step the template is misaligned and the cross-correlation would shift every spike *away* from the true peak.
3. Pack all waveforms and the template into channel-major buffers and compute the optimal shift for every spike simultaneously using normalised circular cross-correlation summed across **all channels** via `XcorrDispatch` (routes to CUDA → HIP → SYCL → OpenMP depending on runtime availability).
4. For each spike where the optimal shift is non-zero:
   a. Advance the timestamp by the shift and update the `.res` file.
   b. Re-extract the shifted waveform from the `.fil` (or `.dat`) file at the new timestamp.
   c. Write the new waveform back to the `.spk` file at the same slot.
   d. Re-project the new waveform through the saved PCA eigenvectors (`.spk.N.evec`) and update the `.fet` file rows.
   e. If the updated timestamp is now out of chronological order with a neighbour, swap all on-disk records (`.res`, `.spk`, `.clu`) and the in-memory `spikesByCluster` row.

Sign convention (consistent with `realign_xcorr.h`): a positive shift `τ` means the spike peak is *late* by `τ` samples relative to the template. Correcting by `newTimestamp = oldTimestamp + τ` moves the spike earlier in the waveform window so its peak aligns with the template.

---

## Files read and written

| File | Access | Description |
|---|---|---|
| `base.xml` / `base.yaml` | read | Session parameters (nChannels, nSamples, peakSampleIndex) |
| `base.spk.N` | read/write | Raw waveforms — shifted in place |
| `base.res.N` | read/write | Timestamps — updated after shifting |
| `base.clu.N` | read/write | Cluster IDs — reordered if timestamp sort order changes |
| `base.fet.N` | read/write | Features — reprojected via PCA if `.spk.N.evec` is available |
| `base.spk.N.evec` | read | PCA eigenvectors written by `process_pca` |

---

## Integration with klusters

After realignment completes, klusters shows a `RealignReviewDialog` with:

- Before/after mean waveform plots for the realigned cluster.
- Counts of spikes shifted and timestamps reordered.
- Accept or Reject buttons (keyboard: Left/Right arrows to navigate, Enter to confirm).

Rejecting the review restores the previous cluster state from an in-memory backup without touching disk.

The maximum shift search radius and minimum score threshold are configurable in **Settings → Preferences → General → Realignment**.

---

## KlustaKwik Phase 1.5

KlustaKwik's chunked CEM mode also performs waveform realignment (Phase 1.5) directly on the `.spk` file after per-chunk sorting, before the global merge step. It uses the same `XcorrDispatch` kernel but operates on all clusters in all chunks in a single batch pass. See `src/klustakwik/CHANGES.md` for details.

---

## Installation

SpikeRealign is compiled as part of the klusters build. See the klusters installation guide in `doc/klusters/`.
