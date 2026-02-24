# klusters — macOS Installation

## Package manager

Install [Homebrew](https://brew.sh):

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## System packages

```bash
brew install cmake ninja qt@6 yaml-cpp pkg-config
```

## Add Qt to PATH

```bash
echo 'export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

On Intel Macs, replace `/opt/homebrew` with `/usr/local`.

## Build libklustersshared first

klusters depends on `libklustersshared`, which must be built from source.

```bash
cd /path/to/neurosuite-3/src

cmake -B build/libklustersshared libklustersshared \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix yaml-cpp)"
cmake --build build/libklustersshared -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build/libklustersshared
```

## Build klusters

```bash
cmake -B build/klusters klusters \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix yaml-cpp)"
cmake --build build/klusters -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build/klusters
```

## Verify

```bash
klusters --version
klusters session.fet.1
```
