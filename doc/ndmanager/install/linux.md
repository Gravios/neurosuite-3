# ndmanager — Linux Installation (Ubuntu / Debian)

## System packages

```bash
sudo apt install \
  cmake build-essential pkg-config \
  qt6-base-dev \
  libxml2-dev \
  libyaml-cpp-dev
```

## Build libklustersshared first

ndmanager depends on `libklustersshared`, which is not available as a distro package.

```bash
cd /path/to/neurosuite-3/src

cmake -B build/libklustersshared libklustersshared \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/libklustersshared -j$(nproc)
sudo cmake --install build/libklustersshared
```

## Build ndmanager

```bash
cmake -B build/ndmanager ndmanager \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ndmanager -j$(nproc)
sudo cmake --install build/ndmanager
```

## Verify

```bash
ndmanager --version
ndmanager templates/jg05-20120316.yaml
```

## Uninstall

```bash
sudo cmake --build build/ndmanager --target uninstall
```

If the uninstall target is absent, remove the files listed in `build/ndmanager/install_manifest.txt`:

```bash
sudo xargs rm -f < build/ndmanager/install_manifest.txt
```
