# ndmanager-plugins — Preprocessing Pipeline

ndmanager-plugins is a collection of command-line tools that convert
raw acquisition data into the formats required for spike sorting, and
then carry those outputs through secondary analyses (drift estimation,
collision decomposition, spike subtraction, re-extraction, source
localisation). It consists of C++ processing binaries (some with
optional CUDA acceleration), Python post-sorting analysis modules, and
Bash pipeline scripts that orchestrate them.

All scripts take a single optional argument: the path to the session
`.yaml` parameter file. When omitted, the current directory name is
used as the session basename. XML parameter files are no longer read
directly by the pipeline — convert legacy files once with
`ndm_xml2yaml`.

---

## Reference

| | |
|---|---|
| **[Pipeline overview](pipeline.md)** | Full per-acquisition-system pipeline diagrams (AlphaOmega, Neuralynx/CED/Amplipex, post-sorting) |
| **[Command reference](commands/)** | Per-command pages: synopsis, options, output files |
| **[File formats](formats/)** | Binary and YAML format reference (`.dat`, `.spk`, `.fet`, `.col`, `.drift`, …) |
| **[Pipeline variants and file extensions](#pipeline-variants-and-file-extensions)** | Raw vs stderiv detection, extension fallback rules |

For task-oriented walkthroughs that span multiple programs, see
[`../workflows/`](../workflows/).

### Acquisition pipeline commands

| Command | Purpose |
|---|---|
| [`ndm_aom2dat`](commands/ndm_aom2dat.md) | AlphaOmega `.mat` (HDF5 v7.3) → `.dat` + `.yaml` |
| [Format conversion](commands/format-conversion.md) | Neuralynx, CED/Spike2, Amplipex converters |
| [Channel manipulation](commands/channel-manipulation.md) | `ndm_resample`, `ndm_mergedat`, `ndm_extractchannels`, `ndm_reorderchannels`, `ndm_recolorchannels`, `ndm_concatenate` |
| [`ndm_setupgroups`](commands/ndm_setupgroups.md) | Build channel groups from probe library |
| [Signal processing](commands/signal-processing.md) | `ndm_hipass`, `ndm_lfp` |
| [Spike detection](commands/spike-detection.md) | `ndm_extractspikes`, `ndm_extractspikes_sdiff`, `ndm_extractspikes_stderiv`, `ndm_spikecleaner`, `ndm_denoiseuniform` |
| [`ndm_spikegrouper`](commands/ndm_spikegrouper.md) | Automatic spike-group refinement |
| [PCA feature extraction](commands/pca.md) | `ndm_pca`, `ndm_pca_stderiv` |
| [`ndm_klustakwik`](commands/ndm_klustakwik.md) | Run KlustaKwik for each spike group |

### Post-sorting commands

| Command | Purpose |
|---|---|
| [`ndm_reextractspikes`](commands/ndm_reextractspikes.md) | Masked second-pass detection + shadow-clustered merge |
| [`ndm_subcluster_unmatched`](commands/ndm_subcluster_unmatched.md) | Re-cluster the unmatched bin from reextract |
| [`kk_build_prior.py`](commands/kk_build_prior.md) | Train a per-probe empirical prior from curation logs |
| [`kk_resolve_prior.py`](commands/kk_resolve_prior.md) | Look up the prior for a session+group |
| [`ndm_stripdat`](commands/ndm_stripdat.md) | Subtract spikes to produce a cleaned `.dat` |
| [`ndm_redetectspikes`](commands/ndm_redetectspikes.md) | Second-round detection on cleaned `.dat` |
| [`ndm_decomposecollisions`](commands/ndm_decomposecollisions.md) | Collision decomposition (`.col.N`) |
| [`ndm_localise`](commands/ndm_localise.md) | Per-spike source localisation (`.loc.N`) |
| [`ndm_estimatedrift`](commands/ndm_estimatedrift.md) | Spatial probe drift estimation (`.drift`) |
| [`ndm_applydrift`](commands/ndm_applydrift.md) | Propagate curated drift to sibling shanks |

### Utilities

| Command | Purpose |
|---|---|
| [Video processing](commands/video.md) | `ndm_extractleds`, `ndm_transcodevideo` |
| [`ndm_xml2yaml`](commands/ndm_xml2yaml.md) | Legacy XML → YAML converter |
| [`ndm_clean`](commands/ndm_clean.md) | Remove intermediate files |
| [`ndm_functions`](commands/ndm_functions.md) | Shared bash helpers (sourced by every script) |

### File formats

| File | Description |
|---|---|
| [`.dat` / `.lfp` / `.fil`](formats/dat.md) | Binary signal files |
| [`.res.N`](formats/res.md) | Spike timestamps |
| [`.spk.N` / `.spkD.N`](formats/spk.md) | Spike waveforms |
| [`.fet.N` / `.fetD.N`](formats/fet.md) | PCA feature vectors |
| [`.pca.N` / `.pcaD.N`](formats/pca.md) | PCA eigenvector basis |
| [`.clu.N`](formats/clu.md) | Cluster assignments |
| [`.col.N`](formats/col.md) | Collision decomposition results |
| [`.drift`](formats/drift.md) | Probe drift trajectories |
| [`.chunks.N`](formats/chunks.md) | Adaptive KlustaKwik chunk boundaries |
| [`.loc.N`](formats/loc.md) | Per-spike source locations |
| [`.probe`](formats/probe.md) | Probe configuration |

---

## Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| CMake, C++20 compiler | Build system | Yes |
| OpenMP | Parallel processing throughout the pipeline | Recommended |
| libhdf5 | AlphaOmega `.mat` (HDF5 v7.3) conversion | Yes |
| libxml2 | Legacy XML parameter-file reading (via `ndm_xml2yaml` only) | Yes |
| yaml-cpp | YAML parameter-file reading | Yes |
| python3-yaml | YAML reading in bash pipeline scripts | Yes |
| numpy | `ndm_estimatedrift`, `ndm_applydrift`, `ndm_decomposecollisions`, `ndm_localise`, `ndm_setupgroups`, `kk_build_prior`, `kk_resolve_prior` | Yes |
| scipy | Sub-pixel parabolic interpolation in `process_estimatedrift.py` | Optional |
| FFmpeg | `process_extractleds`, `ndm_transcodevideo` | Optional |
| CUDA Toolkit ≥ 12.8 | `process_medianfilter`, `process_medianthreshold` | Optional |

The CUDA filter falls back to an OpenMP CPU path automatically when
CUDA is absent.

---

## Pipeline variants and file extensions

Two spike-detection transforms are supported. The choice is encoded
in the filename extension of every downstream artefact. `.clu` and
`.res` have no variant — they are always written under the canonical
extensions regardless of which detection pipeline was used.

| Canonical | stderiv D variant | Writer |
|---|---|---|
| `.res.N` | *(no variant)* | `ndm_extractspikes*`, `ndm_reextractspikes*` |
| `.spk.N` | `.spkD.N` | `ndm_extractspikes` vs `ndm_extractspikes_stderiv` |
| `.fet.N` | `.fetD.N` | `ndm_pca` vs `ndm_pca_stderiv` |
| `.pca.N` | `.pcaD.N` | `ndm_pca` vs `ndm_pca_stderiv` |
| `.clu.N` | *(no variant)* | `ndm_klustakwik`, klusters |

Within a single session, groups can freely mix pipelines. Every
downstream tool that reads waveforms or features auto-detects both
extensions and prefers the D variant when both are present, or picks
the one the group was sorted with. See
[`../../src/klustakwik/CHANGES.md`](../../src/klustakwik/CHANGES.md)
for the fallback logic inside KlustaKwik, and
[`../design/reextract-v2.md`](../design/reextract-v2.md) for the
cross-pipeline re-extract shim.

---

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Success |
| `1` | Output already exists — step skipped (not an error in batch mode) |
| `2` | Input files missing — step skipped |
| `3` | General error |
| `20` | Missing mandatory parameter |

---

## Performance notes

`process_aomconvert` streams HDF5 data in chunks via the HDF5 C
hyperslab API. Default chunk size (10M samples × 32 channels × 2 bytes
≈ 610 MB) is tuned for systems with 64+ GB RAM. Reduce `chunkSamples`
on memory-constrained machines.

`process_medianfilter` (used by `ndm_hipass`) processes data in
configurable chunks. CUDA targets: sm_86 (Ampere), sm_89 (Ada
Lovelace), sm_100 (Hopper), sm_120 (Blackwell). Requires CUDA ≥ 12.8
for sm_120 (RTX 5000 series).

`process_resample`, `process_pca`, `process_pca_stderiv`,
`process_shadowcluster`, `process_reextractspikes`,
`process_reextractspikes_stderiv`, and `process_extractspikes_stderiv`
all use OpenMP. `ndm_pca{,_stderiv}` runs all electrode groups in
parallel with a configurable per-group thread budget
(`threadsPerGroup`).

For broader hardware tuning recipes, see
[`../design/optimization.md`](../design/optimization.md).

---

## Installation

| Platform | GPU | Guide |
|---|---|---|
| Linux (Ubuntu / Debian) | CPU / OpenMP only | [install/linux-cpu.md](install/linux-cpu.md) |
| Linux (Ubuntu / Debian) | NVIDIA CUDA | [install/linux-cuda.md](install/linux-cuda.md) |
| Windows | CPU (native) or full pipeline via WSL2 | [install/windows.md](install/windows.md) |
| macOS | CPU / OpenMP only | [install/macos.md](install/macos.md) |

GPU stack details (CUDA versions, MOK signing, ROCm, oneAPI) are
shared with the other components in
[`../gpu/README.md`](../gpu/README.md).
