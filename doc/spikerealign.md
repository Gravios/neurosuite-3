# SpikeRealign

Standalone spike waveform realignment tool for ndmanager/klusters sessions.

Reads binary `.spk/.res/.clu/.fet/.pca` files, aligns each spike waveform to
the cluster mean template via normalised cross-correlation, and writes the
corrected data back in-place.  File formats are identical to those used by
KlustaKwik and klusters.

## Usage

```
SpikeRealign FileBase ElecNo [Options]
```

### Required arguments

| Argument   | Description                                      |
|------------|--------------------------------------------------|
| `FileBase` | Session base name (e.g. `session` for `session.spk.1`) |
| `ElecNo`   | Electrode group number (e.g. `1`)                |

### Options

| Option              | Default       | Description                                      |
|---------------------|---------------|--------------------------------------------------|
| `-Threshold F`      | `0.70`        | Minimum xcorr score to accept a shift (0–1)      |
| `-Iterations N`     | `2`           | Number of iterative alignment passes             |
| `-MaxShift N`       | `peakSamp/2`  | Search radius in samples                         |
| `-Clusters A,B,C`  | all           | Comma-separated cluster IDs to process           |
| `-Verbose`          | off           | Print per-spike shift details                    |

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

## Files

| File              | Access    | Description                              |
|-------------------|-----------|------------------------------------------|
| `base.xml`        | read      | Session parameters (nChannels, nSamples, peakSampleIndex) |
| `base.spk.N`      | read/write| Raw waveforms — shifted in place         |
| `base.res.N`      | read/write| Timestamps — updated after shifting      |
| `base.clu.N`      | read/write| Cluster IDs — reordered if sort order changes |
| `base.fet.N`      | read/write| Features — reprojected via PCA if `.pca.N` available |
| `base.pca.N`      | read only | PCA eigenvectors written by `process_pca`|

All files are binary.  The tool performs random-access reads and writes
without loading the entire session into memory.

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

### GPU acceleration

The same backend selection as KlustaKwik applies:

| Backend | Requirement                          | Priority |
|---------|--------------------------------------|----------|
| CUDA    | NVIDIA GPU + CUDA toolkit            | 1st      |
| HIP     | AMD GPU + ROCm                       | 2nd      |
| SYCL    | Intel Arc/Xe + oneAPI Base Toolkit   | 3rd      |
| OpenMP  | CPU (always available, fallback)     | last     |

Force a specific backend:

```bash
cmake .. -DUSE_CUDA=OFF -DUSE_HIP=OFF    # SYCL only
cmake .. -DUSE_CUDA=OFF -DUSE_SYCL=OFF  # HIP only
cmake .. -DUSE_HIP=OFF  -DUSE_SYCL=OFF  # CUDA only
cmake .. -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF  # CPU only
```

A `SpikeRealign_cpu` binary is always built alongside any GPU binary as a
portable reference.

## Integration with klusters

In klusters `Settings → Preferences → General → Realignment`:

- **Executable**: path to `SpikeRealign`
- **Arguments**: `-Threshold 0.75 -Iterations 3` (or any other overrides)

The built-in klusters realignment (cluster-by-cluster, interactive) uses the
same algorithm and file formats.  `SpikeRealign` is intended for batch
processing entire sessions outside of the klusters GUI.
