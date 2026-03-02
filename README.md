# neurosuite-3

A modernised, Qt6-compatible fork of the Neurosuite electrophysiology toolchain. All components compile under C++17 with Qt 6 on Ubuntu 24.04, Debian 12, and WSL2. GPU acceleration is available for compute-intensive steps via CUDA, ROCm/HIP, and SYCL.

---

## Components

| Component | Purpose |
|---|---|
| [libklustersshared](doc/libklustersshared/README.md) | Shared library: canonical YAML I/O, shared data types, GUI widgets |
| [ndmanager](doc/ndmanager/README.md) | GUI session manager — edit parameters, run the preprocessing pipeline |
| [ndmanager-plugins](doc/ndmanager-plugins/README.md) | Command-line preprocessing pipeline (format conversion, filtering, PCA, spike detection) |
| [neuroscope](doc/neuroscope/README.md) | Multi-channel signal visualiser |
| [klusters](doc/klusters/README.md) | Interactive manual spike-sorting GUI |
| [klustakwik](doc/klustakwik/README.md) | Automatic spike sorter (Classification EM) |
| [spikerealign](doc/spikerealign/README.md) | Batch spike waveform realignment tool |

---

## Quick start

```bash
# Clone
git clone https://github.com/Gravios/neurosuite-3.git
cd neurosuite-3

# Build and install everything in dependency order (requires cmake, ninja, Qt6, yaml-cpp, libxml2)
./build-neurosuite.sh --prefix /usr/local

# Reload the dynamic linker if needed
echo '/usr/local/lib' | sudo tee /etc/ld.so.conf.d/neurosuite.conf
sudo ldconfig

# Open a session
ndmanager session.yaml
```

Pass `--help` to `build-neurosuite.sh` for the full option list, including `--skip`, `--cuda-arch`, `--no-install`, and `--clean`.

---

## Build order

`libklustersshared` must be installed before ndmanager or klusters. The build script handles this automatically.

```
libklustersshared → ndmanager
                 → klusters
ndmanager-plugins  (independent)
neuroscope         (independent)
klustakwik         (independent)
spikerealign       (independent)
```

---

## Typical workflow

```
Raw acquisition files
        │
        ▼
ndmanager session.yaml       ← edit parameters, then run pipeline
        │  (calls ndm_start)
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
klusters session.yaml        ← manual curation
        │
        ▼
neuroscope session.yaml      ← validation against LFP / events / position
```

---

## Parameter file format

All tools share a single session `.yaml` (or legacy `.xml`) parameter file. Both formats are supported by every GUI application. Use `ndm_xml2yaml` to convert existing XML sessions to YAML.

See [libklustersshared — YAML schema reference](doc/libklustersshared/README.md#yaml-schema-reference) for the full schema. A worked 96-channel silicon probe example is in `templates/jg05-20120316.yaml`.

---

## What's new in neurosuite-3

- **Qt6 / C++17** — all five Qt packages compile cleanly under Qt 6 on Ubuntu 24.04.
- **YAML parameter format** — new YAML schema mirrors the XML schema exactly; `ParameterYamlReader`, `ParameterYamlWriter`, and `ParameterYamlModifier` in `libklustersshared` are the single implementation shared by all applications.
- **GPU acceleration** — KlustaKwik and SpikeRealign support CUDA, ROCm/HIP, and SYCL (Intel Arc). `process_medianfilter` (ndmanager-plugins) supports CUDA.
- **Three-phase chunked CEM** — KlustaKwik can sort long recordings in temporal chunks for improved handling of electrode drift.
- **`ndm_spikegrouper`** — automatic discovery of optimal spike detection channel groups from the high-pass filtered data.
- **`ndm_xml2yaml`** — converts legacy XML parameter files to YAML.
- **Bug fixes** — critical data-loss bug in `getUnits()` fixed; video orientation (rotation/flip/trajectory) no longer lost on YAML save/reload; `ndmanager` video page fields now populate correctly from YAML.

See [CHANGES.md](CHANGES.md) for the full list of fixes and improvements.

---

## Documentation

Per-component documentation is in `doc/`:

- [doc/libklustersshared/](doc/libklustersshared/README.md)
- [doc/ndmanager/](doc/ndmanager/README.md)
- [doc/ndmanager-plugins/](doc/ndmanager-plugins/README.md)
- [doc/neuroscope/](doc/neuroscope/README.md)
- [doc/klusters/](doc/klusters/README.md)
- [doc/klustakwik/](doc/klustakwik/README.md)
- [doc/spikerealign/](doc/spikerealign/README.md)

Each component doc directory contains an `install/` subdirectory with platform-specific build instructions.
