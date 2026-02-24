# neuroscope — macOS Installation

## Package manager

Install [Homebrew](https://brew.sh):

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## System packages

```bash
brew install cmake ninja qt@6 libxml2 yaml-cpp pkg-config
```

### Optional: FFmpeg (video display)

```bash
brew install ffmpeg
```

## Add Qt to PATH

```bash
echo 'export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

On Intel Macs, replace `/opt/homebrew` with `/usr/local`.

## Build

NeuroScope does **not** depend on `libklustersshared`.

```bash
cd /path/to/neurosuite-3/src

cmake -B build/neuroscope neuroscope \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix libxml2);$(brew --prefix yaml-cpp)"
cmake --build build/neuroscope -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build/neuroscope
```

## Verify

```bash
neuroscope --version
```
