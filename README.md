# neurosuite-3

Spike sorting and electrophysiology visualization — a modern **Qt 6 / C++20** revival of the Neurosuite toolchain for extracellular recordings.

[![Linux](https://github.com/Gravios/neurosuite-3/actions/workflows/ci-linux.yml/badge.svg?branch=main)](https://github.com/Gravios/neurosuite-3/actions/workflows/ci-linux.yml)
[![Windows](https://github.com/Gravios/neurosuite-3/actions/workflows/ci-windows.yml/badge.svg?branch=main)](https://github.com/Gravios/neurosuite-3/actions/workflows/ci-windows.yml)
[![macOS](https://github.com/Gravios/neurosuite-3/actions/workflows/ci-macos.yml/badge.svg?branch=main)](https://github.com/Gravios/neurosuite-3/actions/workflows/ci-macos.yml)
[![Docs](https://github.com/Gravios/neurosuite-3/actions/workflows/docs.yml/badge.svg?branch=main)](https://github.com/Gravios/neurosuite-3/actions/workflows/docs.yml)

neurosuite-3 takes you from a raw recording to sorted single units: convert and filter the signal, detect and automatically sort spikes, curate the result by hand, and inspect everything against the LFP, events, and the animal's position. Optional GPU acceleration (CUDA / ROCm / SYCL) sits over a CPU fallback that is always available.

## Platforms

The badges above show the latest CI run on `main` for each platform.

| Platform | Status |
|---|---|
| **Linux** — Ubuntu 24.04 and newer, Debian 12; WSL2 works | Reference platform. CI builds everything and runs the test suite on Ubuntu 24.04, 25.10, 26.04 and 26.10 (Qt 6.4 is the oldest Qt supported). |
| **Windows 10 / 11** — Visual Studio 2022 | Klusters, NeuroScope, NDManager and the processing programs build natively; the `ndm_*` steps run from cmd and PowerShell through Git Bash. KiloKlustaKwik is not built on Windows yet: run it, or the whole pipeline, under WSL2. CI builds with MSVC (no tests yet). |
| **macOS** — Apple silicon, Homebrew | Builds with Homebrew's Qt 6 and libraries; CI builds it on macOS 15, its tests not yet required to pass. A Metal (Apple GPU) backend is being added (so far the high-pass filters, noise thresholds and channel grouping); the spectrogram FFT uses Apple's Accelerate. The `ndm_*` steps need bash 4 (`brew install bash`). |

Step-by-step installation for each: **[INSTALL.md](doc/INSTALL.md)**.

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

That is the Linux route (macOS: the same commands with Homebrew). On **Windows**, start with `scripts\install-deps-windows.bat`, which installs the compiler, the libraries and Qt, then `cmake --preset windows`, `cmake --build --preset windows` and `cmake --install build`; the install puts the programs on your PATH. Every platform, step by step: **[INSTALL.md](doc/INSTALL.md)**.

---

## The programs

| Program | What it does |
|---|---|
| **[ndmanager](doc/ndmanager/README.md)** | Set up a session — acquisition, probes and channel groups, each step's parameters with their help — and lay out the processing pipeline as a graph |
| **[ndmanager-plugins](doc/ndmanager-plugins/README.md)** | The command-line pipeline (`ndm_*`) — conversion, filtering, spike detection and extraction, PCA, sorting, and the post-sorting steps (re-extraction, spike subtraction, drift, collisions) |
| **[kiloklustakwik](doc/kiloklustakwik/README.md)** | Automatic spike sorter (classification EM, chunked for long recordings) |
| **[klusters](doc/klusters/README.md)** | Interactive spike-sorting and curation GUI |
| **[neuroscope](doc/neuroscope/README.md)** | Multi-channel signal, spectral, event & position viewer |

External sorters plug into NDManager through the ndm plugin protocol: the [fiber-kit](https://github.com/Gravios/fiber-kit) sorter runs as `ndm_fiber-kit` (see [`ndm_klustakwik`](doc/ndmanager-plugins/commands/ndm_klustakwik.md#external-sorter-ndm_fiber-kit)).

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
| neuroscope | CUDA (cuFFT) | spectrogram FFTs |
| `ndm_hipass` | CUDA · Metal (macOS) / OpenMP | zero-phase Butterworth filter (default; the CUDA kernel is not yet validated on hardware) · median filter |
| `ndm_detectspikes` · `ndm_spikegrouper` | CUDA · Metal (macOS) | noise thresholds, channel grouping |
| `ndm_extractemg` | CUDA / HIP / SYCL | EMG extraction |
| `ndm_decomposecollisions` | CUDA (opt-in) | collision decomposition fit |

Toolkit setup (CUDA including Blackwell / sm_120, AMD ROCm, Intel oneAPI/SYCL): [BUILD.md §4](doc/BUILD.md) and [doc/gpu/](doc/gpu/README.md).

---

## Highlights

- **Curation in Klusters** — [hierarchical clustering](doc/klusters/hierarchical-clustering.md) into fibers and atoms, template curation in the feature space, t-SNE and oblique views, multi-lasso, DipSplit and watershed splits, merge recommendations and drift / template / residual matrices; a rebindable keyboard and mouse and a right-click radial menu.
- **Safe sessions** — Klusters saves all-or-nothing and skips unchanged files, autosaves for crash recovery, and keeps a JSON-lines log of curation actions (`.cur`) that the curation and prior-training tools learn from.
- **NDManager** — a Pipeline tab that builds and saves pipelines as a graph, plugins grouped by purpose, each parameter with its help and an **Add** list of the ones a step reads, session templates you can export and apply, and a probe library (43 NeuroNexus models) with a geometry editor.
- **Recording formats** — Neuralynx, CED, AlphaOmega and Plexon (`.plx` / `.pl2`) recordings convert to `.dat`; the AlphaOmega and Plexon converters also fill in the session YAML from the recording.
- **Detection and sorting** — a zero-phase Butterworth high-pass by default; spatial-derivative detection methods (`sdiff`, `stderiv`); every per-group file named for its method (`SESSION.<type>.<method>.N`), so methods coexist in one session; three-tier KiloKlustaKwik parameters (per group → session → defaults) and chunked CEM for long recordings.
- **After sorting** — re-extraction at a lower threshold, spike subtraction (`ndm_stripdat`) and re-detection, drift estimation and correction, collision decomposition, learned curation (`ndm_autoklusta`) and empirical priors from curation logs.
- **Everywhere** — Qt 6 / C++20 across every app, one YAML parameter format, native Windows and macOS builds, and current manuals built into the apps (**F1**) and [online](https://gravios.github.io/neurosuite-3/).

See [CHANGELOG.md](CHANGELOG.md) for the full history.

---

## Contributing & license

Patch discipline, coding standards, and the build/verify workflow are in **[CONTRIBUTING.md](.github/CONTRIBUTING.md)**; the architecture and internals in **[DEVELOPER_GUIDE.md](doc/dev/DEVELOPER_GUIDE.md)**.

neurosuite-3 is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License, version 3 or (at your option) any later version** — see [LICENSE](LICENSE). It comes with no warranty.

A few bundled third-party parts keep their own compatible licenses: the vendored libsamplerate fallback (GPL-2.0-or-later), the KDE-derived page-dialog classes in `libklustersshared/src/gui/page/` (LGPL-2.0-or-later), a Qt-derived helper in `ndmanager` (LGPL-2.1 / GPL-3.0), and the Oxygen icons (LGPL-3.0-or-later).
