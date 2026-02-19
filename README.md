# Neurosuite

A modernised, Qt6-compatible fork of the Neurosuite electrophysiology analysis toolchain. All components compile under **C++17** with **Qt 6** on Ubuntu 24.04, Debian 12, and WSL 2. GPU acceleration (CUDA 12.8+) is available for the most compute-intensive preprocessing steps.

---

## Repository layout

```
neurosuite/src/
├── libklustersshared/      Shared Qt6 widget library (dependency of ndmanager and klusters)
├── ndmanager/              GUI session manager — opens .xml parameter files, dispatches plugins
├── ndmanager-plugins/      Command-line preprocessing pipeline (C++ binaries + shell scripts)
│   ├── src/                C++/CUDA source for process_* binaries
│   └── scripts/            ndm_* bash pipeline scripts
├── neuroscope/             Wideband/LFP/spike waveform visualiser
├── klusters/               Manual spike-sorting GUI
├── klustakwik/             Automatic spike-sorting (EM clustering, optional CUDA)
├── nphys-data/             Shared MIME-type icons and desktop integration files
├── templates/              Example .xml parameter file for a 96-channel silicon probe session
├── doc/                    Per-component manuals and migration notes
└── scripts/                Utility scripts (backtrace helper, ndmanager-safe wrapper)
```

---

## Components

| Component | Purpose | Manual |
|---|---|---|
| `ndmanager` | GUI session manager — edits `.xml` parameter files and launches preprocessing plugins | [doc/ndmanager.md](doc/ndmanager.md) |
| `ndmanager-plugins` | Preprocessing pipeline: format conversion, filtering, spike detection, PCA | [doc/ndmanager-plugins.md](doc/ndmanager-plugins.md) |
| `neuroscope` | Multi-channel raw signal, LFP, spike and event visualiser | [doc/neuroscope.md](doc/neuroscope.md) |
| `klusters` | Interactive manual spike-sorting GUI | [doc/klusters.md](doc/klusters.md) |
| `klustakwik` | Automatic spike-sorting via EM clustering (optional GPU acceleration) | [doc/klustakwik.md](doc/klustakwik.md) |
| `libklustersshared` | Shared Qt6 widget library — build dependency only, not used directly | — |

---

## Build

### Prerequisites

```bash
sudo apt install \
  cmake build-essential pkg-config \
  qt6-base-dev libqt6svg6-dev \
  libxml2-dev \
  libgsl-dev \
  libgomp1 \
  autoconf automake libtool make \
  xsltproc docbook-xsl \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  ffmpeg
```

`libklustersshared` must be built from source first — no distro package exists. CUDA is optional; both `process_medianfilter` and `process_medianthreshold` fall back to OpenMP CPU paths when CUDA is absent. See `doc/ndmanager-plugins.md` for CUDA installation instructions.

### Build order

Components must be built in dependency order. `libklustersshared` must be installed before `ndmanager` or `klusters`.

```bash
# 1. libklustersshared (required first)
cmake -B build/libklustersshared libklustersshared -DCMAKE_BUILD_TYPE=Release
cmake --build build/libklustersshared -j$(nproc)
sudo cmake --install build/libklustersshared

# 2. ndmanager-plugins (no dependency on libklustersshared)
cmake -B build/ndmanager-plugins ndmanager-plugins -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager-plugins -j$(nproc)
sudo cmake --install build/ndmanager-plugins

# 3. ndmanager
cmake -B build/ndmanager ndmanager -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager -j$(nproc)
sudo cmake --install build/ndmanager

# 4. neuroscope
cmake -B build/neuroscope neuroscope -DCMAKE_BUILD_TYPE=Release
cmake --build build/neuroscope -j$(nproc)
sudo cmake --install build/neuroscope

# 5. klusters
cmake -B build/klusters klusters -DCMAKE_BUILD_TYPE=Release
cmake --build build/klusters -j$(nproc)
sudo cmake --install build/klusters

# 6. klustakwik (CPU-only)
cmake -B build/klustakwik klustakwik -DUSE_CUDA=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/klustakwik -j$(nproc)
sudo cmake --install build/klustakwik

# 6. klustakwik (with CUDA — requires CUDA ≥ 12.8)
cmake -B build/klustakwik klustakwik \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.1/bin/nvcc \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/klustakwik -j$(nproc)
sudo cmake --install build/klustakwik
```

---

## Data directory structure

All tools are session-oriented. A **session** is a single continuous recording identified by a base name (e.g. `rat07-2012-03-16`). The session name is also the name of the directory that holds it.

### Single-session layout

```
rat07-2012-03-16/
├── rat07-2012-03-16.xml      # Parameter file — central manifest for all tools
├── rat07-2012-03-16.dat      # Wideband binary (int16, multiplexed channels)
├── rat07-2012-03-16.lfp      # Downsampled copy of .dat for LFP analysis
├── rat07-2012-03-16.fil      # High-pass filtered .dat for spike sorting
├── rat07-2012-03-16.res.1    # Spike timestamps, electrode group 1 (sample indices)
├── rat07-2012-03-16.spk.1    # Spike waveforms, electrode group 1 (int16)
├── rat07-2012-03-16.fet.1    # PCA feature vectors, electrode group 1
├── rat07-2012-03-16.clu.1    # Cluster assignments, electrode group 1
├── rat07-2012-03-16.pos      # Animal position (x,y per video frame)
├── rat07-2012-03-16.spots    # Raw LED detections from video
└── rat07-2012-03-16.cat.evt  # Subsession boundary timestamps
```

### Multi-session layout (Neuralynx example)

```
experiment/
├── rat07-2012-03-16/             # Processed files live here
│   ├── rat07-2012-03-16.xml
│   ├── rat07-2012-03-16.dat      # Concatenated across all subsessions
│   └── rat07-2012-03-16.cat.evt
│
├── rat07-2012-03-16-nlx/         # Raw Neuralynx files (suffix = "nlx")
│   ├── rat07-2012-03-16-nlx.ncs
│   ├── rat07-2012-03-16-nlx.nev
│   └── rat07-2012-03-16-nlx.nvt
│
└── rat07-2012-03-16-vid/         # Video files (suffix = "vid")
    └── rat07-2012-03-16-vid.avi
```

The suffix (e.g. `nlx`, `vid`, `smr`) is declared in the `.xml` parameter file and tells each `ndm_*` script where to find its input files. For CED/Spike2 systems use a `-smr/` directory; for Amplipex use `.tsp` / `.meta` files in the suffixed directory.

---

## Typical workflow

```
Raw acquisition files
        │
        ▼
ndm_start template.xml       ← format conversion, filtering, spike extraction, PCA
        │
        ▼
session.fet.1 … session.fet.N
        │
        ▼
KlustaKwik session N         ← automatic cluster assignment
        │
        ▼
session.clu.1 … session.clu.N
        │
        ▼
klusters session.xml         ← manual curation
        │
        ▼
neuroscope session.xml       ← visual validation against LFP / events / position
```

---

## License

GNU General Public License v3. See `COPYING` and `GPL-3.0.txt` in each component directory.

Original authors: Lynn Hazan, Michaël Zugaro, and contributors.
