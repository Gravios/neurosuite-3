# spikerealign — Batch Waveform Realignment

SpikeRealign is a standalone batch tool that aligns spike waveforms to the cluster mean template via normalised cross-correlation, correcting for systematic jitter in threshold-crossing detection. It reads and writes binary `.spk/.res/.clu/.fet` files in-place using the same formats as KlustaKwik and klusters.

The klusters interactive realignment (cluster-by-cluster from the GUI, via `RealignWorker`) uses the same cross-correlation algorithm from `realign_xcorr.h` internally. SpikeRealign is intended for batch processing entire sessions outside the klusters GUI, or for scripted pipelines.

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| C++17 compiler | Build | Yes |
| CMake ≥ 3.21 | Build | Yes |
| OpenMP | CPU fallback for cross-correlation | Recommended |
| CUDA Toolkit ≥ 11 | NVIDIA GPU acceleration | Optional |
| ROCm / HIP SDK ≥ 5.0 | AMD GPU acceleration | Optional |
| Intel oneAPI Base Toolkit ≥ 2023.1 | Intel Arc/Xe GPU acceleration | Optional |

GPU backend selection is identical to KlustaKwik: CUDA > HIP > SYCL auto-detected in priority order; every GPU build produces `SpikeRealign_cpu` as a CPU-only fallback.

---

## Usage

```
SpikeRealign FileBase ElecNo [options]
```

### Options

| Option | Default | Description |
|---|---|---|
| `-Threshold F` | `0.70` | Minimum cross-correlation score to accept a shift (0–1). Higher = stricter |
| `-Iterations N` | `2` | Number of iterative alignment passes |
| `-MaxShift N` | `peakSamp/2` | Search radius in samples |
| `-Clusters A,B,C` | all | Comma-separated cluster IDs to process |
| `-Verbose` | off | Print per-spike shift details |

### Examples

```bash
# Realign all clusters in group 1 with defaults
SpikeRealign session 1

# Tighten threshold, more iterations
SpikeRealign session 1 -Threshold 0.80 -Iterations 3

# Only clusters 3 and 7, extended search radius
SpikeRealign session 1 -Clusters 3,7 -MaxShift 10

# Full verbose output
SpikeRealign session 1 -Verbose
```

---

## Algorithm

The realignment runs iteratively for `-Iterations` passes. Each pass:

1. Computes the cluster mean template from all spikes in the cluster.
2. For each spike, slides the waveform over the template within `±MaxShift` samples and computes the normalised cross-correlation at each shift position.
3. If the best-shift correlation exceeds `Threshold`, the spike's stored waveform is shifted by that amount and its timestamp (`res`) updated accordingly.
4. After shifting, if the new timestamp order no longer matches the old sort order, `clu` entries are reordered to maintain timestamp-sorted order. The count of these sort corrections is reported as "swapped" in verbose output.
5. If a `.pca.N` file is present, PCA features in `.fet.N` are reprojected using the stored eigenvectors after all shifts are applied.

Multiple iterations converge the template toward a well-aligned mean; two iterations is usually sufficient.

---

## Files read and written

| File | Access | Description |
|---|---|---|
| `base.xml` / `base.yaml` | read | Session parameters (nChannels, nSamples, peakSampleIndex) |
| `base.spk.N` | read/write | Raw waveforms — shifted in place |
| `base.res.N` | read/write | Timestamps — updated after shifting |
| `base.clu.N` | read/write | Cluster IDs — reordered if timestamp sort order changes |
| `base.fet.N` | read/write | Features — reprojected via PCA if `.pca.N` available |
| `base.pca.N` | read | PCA eigenvectors written by `process_pca` |

All file access is random-read / sequential-write. The tool does not load the entire session into memory.

---

## Integration with klusters

The same realignment algorithm is also available interactively inside klusters. After realignment completes, klusters shows a `RealignReviewDialog` with:

- Before/after mean waveform plots for the realigned cluster.
- Counts of spikes shifted and timestamps reordered.
- Accept or Reject buttons (keyboard: Left/Right arrows to navigate, Enter to confirm).

Rejecting the review restores the previous cluster state from an in-memory backup without touching disk.

Configure the standalone batch tool path and default arguments in **Settings → Preferences → General → Realignment**:

- **Executable**: path to `SpikeRealign`
- **Arguments**: `-Threshold 0.75 -Iterations 3`

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Linux (Ubuntu / Debian) | AMD ROCm / HIP | [install/linux-hip.md](install/linux-hip.md) |
| Linux (Ubuntu / Debian) | Intel Arc / SYCL (bare metal) | [install/linux-sycl.md](install/linux-sycl.md) |
| Linux — WSL2 | Intel Arc / SYCL | [install/wsl2-sycl.md](install/wsl2-sycl.md) |
| Windows (native) | CPU / OpenMP | [install/windows-cpu.md](install/windows-cpu.md) |
| Windows (native) | NVIDIA CUDA | [install/windows-cuda.md](install/windows-cuda.md) |
| Windows (native) | AMD HIP | [install/windows-hip.md](install/windows-hip.md) |
| Windows (native) | Intel Arc / SYCL | [install/windows-sycl.md](install/windows-sycl.md) |
| macOS | CPU / OpenMP only (no GPU support) | [install/macos.md](install/macos.md) |
