# neurosuite-3

Spike sorting and electrophysiology visualization — a modern **Qt 6 / C++20** revival of the Neurosuite toolchain for extracellular recordings.

[![CI](https://github.com/Gravios/neurosuite-3/actions/workflows/ci.yml/badge.svg)](https://github.com/Gravios/neurosuite-3/actions/workflows/ci.yml)
[![Docs](https://github.com/Gravios/neurosuite-3/actions/workflows/docs.yml/badge.svg)](https://github.com/Gravios/neurosuite-3/actions/workflows/docs.yml)

neurosuite-3 takes you from a raw recording to sorted single units: convert and filter the signal, detect and automatically sort spikes, curate the result by hand, and inspect everything against the LFP, events, and the animal's position. It builds on Linux (Ubuntu 24.04 / Debian 12; WSL2 works) and natively on Windows 10/11 (everything except KiloKlustaKwik, which runs under WSL2); macOS configures and builds but is not checked in CI — see [INSTALL.md](doc/INSTALL.md). Optional GPU acceleration (CUDA / ROCm / SYCL) sits over a CPU fallback that is always available.

## Start here

| If you want to… | Go to |
|---|---|
| **Sort your first recording** | [First-time spike sort](doc/workflows/first-time-sort.md) — the end-to-end walkthrough |
| **Install / build it** | [Installing neurosuite-3](doc/INSTALL.md) — Linux, Windows, macOS; [BUILD.md](doc/BUILD.md) for options |
| **Learn the apps** | [User & pipeline docs](doc/README.md) · [NeuroScope handbook](doc/site/docs/handbook/01-introduction.md) — also built into the apps: **Help ▸ Handbook** (F1) |
| **Look up a term** | [Glossary](doc/GLOSSARY.md) |
| **Work on the code** | [Developer Guide](doc/dev/DEVELOPER_GUIDE.md) · [Contributing](.github/CONTRIBUTING.md) |

The full documentation is also published online: **https://gravios.github.io/neurosuite-3/**

---

## Quick start

```bash
git clone https://github.com/Gravios/neurosuite-3.git
cd neurosuite-3

# One step: install system dependencies, then build everything in dependency order
cmake -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DNS_INSTALL_DEPS=ON
cmake --build build --parallel
cmake --install build
```

`-DNS_INSTALL_DEPS=ON` installs the required packages for you (apt / Homebrew / vcpkg); drop it if you'd rather manage them yourself. Choosing a build type, picking or disabling GPU backends, skipping a component (e.g. `-DNS_SKIP_KILOKLUSTAKWIK=ON`), the macOS Qt path and the Windows/vcpkg setup, a sanitizer build, and reproducing CI — all live in **[BUILD.md](doc/BUILD.md)**.

That is the Linux route (macOS: the same commands with Homebrew). On **Windows**, start with `scripts\install-deps-windows.bat`, which installs the compiler, the libraries and Qt, then `cmake --preset windows`. Every platform, step by step: **[INSTALL.md](doc/INSTALL.md)**.

---

## The programs

| Program | What it does |
|---|---|
| **[ndmanager](doc/ndmanager/README.md)** | Set up a session and run the preprocessing pipeline |
| **[ndmanager-plugins](doc/ndmanager-plugins/README.md)** | The command-line pipeline — conversion, filtering, PCA, spike detection (`ndm_*`) |
| **[kiloklustakwik](doc/kiloklustakwik/README.md)** | Automatic spike sorter (classification EM) |
| **[klusters](doc/klusters/README.md)** | Interactive manual spike-sorting & curation GUI |
| **[neuroscope](doc/neuroscope/README.md)** | Multi-channel signal, event & position viewer |

Shared foundations: **libklustersshared** (Qt widgets, the shared `input::` key/mouse engine, YAML parameter I/O) and **[libneurosuite-core](src/libneurosuite-core/docs/FILE_FORMATS.md)** (the Qt-free `neurofileio` reader/writer for every on-disk format). Spike-waveform realignment (`spikerealign`) is built into klusters and KiloKlustaKwik.

All tools share one session **`.yaml`** parameter file. The GUIs read YAML only (ndmanager refuses a legacy `.xml` and tells you to convert it; Klusters also opens the old `.par` files); only the `ndm_*` scripts still fall back to reading a legacy `.xml`. Convert legacy `.xml` files with `ndm_xml2yaml`. A starting point is [`templates/template.yaml`](templates/template.yaml).

---

## Workflows

Task-based walkthroughs that span the toolchain — begin with the first one:

- [First-time spike sort](doc/workflows/first-time-sort.md) — acquire → preprocess → sort → curate
- [Cluster curation](doc/workflows/cluster-curation.md) — split / merge / realign / nudge in Klusters
- [Re-extract with a lower threshold](doc/workflows/re-extract-lower-threshold.md) — recover weak units
- [Iterative refinement](doc/workflows/iterative-refinement.md) — strip → redetect → re-sort
- [Drift correction](doc/workflows/drift-correction.md) · [Collision decomposition](doc/workflows/collision-decomposition.md) · [Empirical priors](doc/workflows/empirical-priors.md)

---

## GPU acceleration

Optional and auto-detected — a CPU/OpenMP build is always produced, so no GPU is required. When a toolkit is present at build time:

| Component | Backends | Accelerates |
|---|---|---|
| kiloklustakwik | CUDA / HIP / SYCL | CEM E-step + waveform realignment |
| klusters | CUDA / HIP / SYCL | Grouping Assistant + interactive realignment |
| `ndm_hipass` | CUDA / OpenMP | zero-phase Butterworth filter (default; the CUDA kernel is not yet validated on hardware) · median filter |
| `ndm_extractspikes` · `ndm_spikegrouper` | CUDA | thresholding, channel grouping |

Toolkit setup (CUDA including Blackwell / sm_120, AMD ROCm, Intel oneAPI/SYCL): [BUILD.md §4](doc/BUILD.md) and [doc/gpu/](doc/gpu/README.md).

---

## Highlights

- **AlphaOmega support** — `ndm_aom2dat` converts `.mat` recordings and writes a calibrated session YAML.
- **Cleaner high-pass filtering** — `ndm_hipass` now defaults to a zero-phase Butterworth filter; set `filter: median` to keep the old median filter.
- **Smarter auto-sorting** — three-tier KiloKlustaKwik parameters (per-probe → session → defaults) and chunked CEM for long recordings.
- **Post-sorting pipeline** — spike subtraction (`ndm_stripdat`), second-pass re-detection, drift estimation, and collision decomposition.
- **Modern stack** — Qt 6 / C++20 across every app, one YAML parameter format, and a 50-probe NeuroNexus library.

See [CHANGELOG.md](CHANGELOG.md) for the full history.

---

## Contributing & license

Patch discipline, coding standards, and the build/verify workflow are in **[CONTRIBUTING.md](.github/CONTRIBUTING.md)**; the architecture and internals in **[DEVELOPER_GUIDE.md](doc/dev/DEVELOPER_GUIDE.md)**.

neurosuite-3 is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License, version 3 or (at your option) any later version** — see [LICENSE](LICENSE). It comes with no warranty.

A few bundled third-party parts keep their own compatible licenses: the vendored libsamplerate fallback (GPL-2.0-or-later), the KDE-derived page-dialog classes in `libklustersshared/src/gui/page/` (LGPL-2.0-or-later), a Qt-derived helper in `ndmanager` (LGPL-2.1 / GPL-3.0), and the Oxygen icons (LGPL-3.0-or-later).
