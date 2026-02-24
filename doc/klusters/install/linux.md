# klusters — Linux Installation (Ubuntu / Debian)

## System packages

```bash
sudo apt install \
  cmake build-essential pkg-config \
  qt6-base-dev libqt6svg6-dev \
  libyaml-cpp-dev
```

## Build libklustersshared first

klusters depends on `libklustersshared`, which is not available as a distro package.

```bash
cd /path/to/neurosuite-3/src

cmake -B build/libklustersshared libklustersshared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/libklustersshared -j$(nproc)
sudo cmake --install build/libklustersshared
```

## Build klusters

```bash
cmake -B build/klusters klusters \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/klusters -j$(nproc)
sudo cmake --install build/klusters
```

## Verify

```bash
klusters --version
klusters session.fet.1
```

## Uninstall

```bash
sudo xargs rm -f < build/klusters/install_manifest.txt
```
