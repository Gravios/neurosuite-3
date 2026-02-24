# ndmanager — macOS Installation

## Package manager

Install [Homebrew](https://brew.sh):

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

## System packages

```bash
brew install cmake ninja qt@6 libxml2 yaml-cpp pkg-config
```

Qt6 is installed to `/opt/homebrew/opt/qt@6` (Apple Silicon) or `/usr/local/opt/qt@6` (Intel). Add it to PATH for CMake to find it:

```bash
echo 'export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

On Intel Macs, replace `/opt/homebrew` with `/usr/local` throughout.

## Build libklustersshared first

ndmanager depends on `libklustersshared`, which must be built from source.

```bash
cd /path/to/neurosuite-3/src

cmake -B build/libklustersshared libklustersshared \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix libxml2);$(brew --prefix yaml-cpp)"
cmake --build build/libklustersshared -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build/libklustersshared
```

## Build ndmanager

```bash
cmake -B build/ndmanager ndmanager \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6);$(brew --prefix libxml2);$(brew --prefix yaml-cpp)"
cmake --build build/ndmanager -j$(sysctl -n hw.logicalcpu)
sudo cmake --install build/ndmanager
```

## Create an app bundle (optional)

To produce a self-contained `.app` bundle for distribution:

```bash
macdeployqt /usr/local/bin/ndmanager.app -dmg
```

If CMake does not install an `.app` bundle, the binary is at `/usr/local/bin/ndmanager` and can be run directly from the terminal.

## Verify

```bash
ndmanager --version
```
