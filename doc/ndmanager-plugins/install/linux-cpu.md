# ndmanager-plugins — Linux Installation, CPU / OpenMP

All processing binaries build with OpenMP parallelism. CUDA is not required.

## System packages

```bash
sudo apt install \
  cmake build-essential pkg-config \
  libxml2-dev \
  libyaml-cpp-dev \
  python3-yaml \
  libgomp1
```

### Optional: FFmpeg (video tools)

```bash
sudo apt install \
  libavcodec-dev libavformat-dev \
  libavutil-dev libswscale-dev \
  ffmpeg
```

Without FFmpeg, `process_extractleds` and `ndm_transcodevideo` are not built. All other pipeline tools are unaffected.

## Build

```bash
cd /path/to/neurosuite-3/src

cmake -B build/ndmanager-plugins ndmanager-plugins \
  -DUSE_CUDA=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager-plugins -j$(nproc)
sudo cmake --install build/ndmanager-plugins
```

## Verify

```bash
which ndm_start ndm_hipass ndm_pca
process_medianfilter --version
```

`process_medianfilter` will print something like:

```
process_medianfilter (OpenMP, N threads)
```

## Performance

`process_medianfilter` will use all available cores via OpenMP. To limit thread count:

```bash
export OMP_NUM_THREADS=8
```

## Uninstall

```bash
sudo xargs rm -f < build/ndmanager-plugins/install_manifest.txt
```
