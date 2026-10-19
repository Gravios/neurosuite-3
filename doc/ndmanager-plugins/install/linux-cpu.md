# ndmanager-plugins — Linux Installation, CPU / OpenMP

The plugins are built and installed with the rest of neurosuite-3: follow
**[Installing neurosuite-3 → Linux](../../INSTALL.md#linux)**. That build also
produces KiloKlustaKwik, which `ndm_klustakwik` runs. No GPU toolkit is
needed; every processing program has an OpenMP CPU path.

## System packages

`-DNS_INSTALL_DEPS=ON` installs the required packages. To install them
yourself, the ones the plugins need are:

```bash
sudo apt install \
  cmake build-essential pkg-config \
  libxml2-dev libyaml-cpp-dev libgsl-dev \
  python3-yaml python3-numpy python3-scipy \
  libgomp1
```

`libgsl-dev` is required: `process_pca` and `process_pca_multichannel` do
`find_package(GSL REQUIRED)`, so configure fails without it.

### Optional packages

| Package | Needed by | Without it |
|---|---|---|
| `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev` | `process_extractleds` (`ndm_extractleds`) | `process_extractleds` is skipped at configure time |
| `libhdf5-dev` | `process_aomconvert` (`ndm_aom2dat`) | `process_aomconvert` is skipped at configure time |
| `python3-sklearn python3-joblib` | `ndm_autoklusta` | `ndm_autoklusta` stops with an error |

`ndm_transcodevideo` is a script and is always installed; it runs the
`avconv` and `avprobe` programs (libav), which the `ffmpeg` package does not
provide.

## Verify

```bash
command -v ndm_start ndm_hipass ndm_pca process_butterworth process_pca
```

The processing programs have no `--version` option; run one without
arguments to see its usage.

## Performance

The processing programs use all cores through OpenMP. To limit the thread
count:

```bash
export OMP_NUM_THREADS=8
```

## Advanced: the plugins on their own

The plugins also configure as a project of their own. This builds the
`process_*` programs and installs the `ndm_*` scripts, but **not
KiloKlustaKwik**, so `ndm_klustakwik` will not run unless KiloKlustaKwik is
installed separately:

```bash
cd /path/to/neurosuite-3
cmake -S src/ndmanager-plugins -B build/ndmanager-plugins -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager-plugins -j$(nproc)
sudo cmake --install build/ndmanager-plugins
```

`-DUSE_CUDA=OFF` does not give a CPU-only build here: it only affects
`process_extractemg`. See the [CUDA guide](linux-cuda.md#cpu-only-build-with-cuda-installed).
Uninstall with `sudo xargs rm -f < build/ndmanager-plugins/install_manifest.txt`.
