# neuroscope — Linux Installation (Ubuntu / Debian)

## System packages

```bash
sudo apt install \
  cmake build-essential pkg-config \
  qt6-base-dev \
  libxml2-dev \
  libyaml-cpp-dev
```

### Optional: FFmpeg (video support)

```bash
sudo apt install \
  libavcodec-dev libavformat-dev \
  libavutil-dev libswscale-dev \
  ffmpeg
```

Without FFmpeg, NeuroScope builds without video display support. All other features are unaffected.

## Build

```bash
cd /path/to/neurosuite-3/src

cmake -B build/neuroscope neuroscope \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/neuroscope -j$(nproc)
sudo cmake --install build/neuroscope
```

## Verify

```bash
neuroscope --version
neuroscope templates/jg05-20120316.yaml
```

## Uninstall

```bash
sudo xargs rm -f < build/neuroscope/install_manifest.txt
```
