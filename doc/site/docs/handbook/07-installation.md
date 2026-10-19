# Installation

## How to obtain NeuroScope {#getting-neuroscope}

NeuroScope is part of neurosuite-3, together with Klusters, NDManager, the NDManager processing plugins and KiloKlustaKwik. The source is at <https://github.com/Gravios/neurosuite-3>; all the programs are built and installed together in one CMake build.

## Requirements

Qt 6.4 or newer, yaml-cpp and libxml2, a C++20 compiler (GCC, Clang, MSVC 2022 or Intel oneAPI) and CMake 3.22 or newer. The build can install the dependencies itself (see below). Linux is the reference platform; Windows 10/11 and macOS are supported.

## Compilation and Installation {#compilation}

On Linux and macOS:

    % git clone https://github.com/Gravios/neurosuite-3.git
    % cd neurosuite-3
    % cmake -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DNS_INSTALL_DEPS=ON
    % cmake --build build --parallel
    % cmake --install build

`-DNS_INSTALL_DEPS=ON` installs the dependencies with apt or Homebrew; leave it out to install them yourself.

On Windows, in the repository, run `scripts\install-deps-windows.bat`, which installs Visual Studio's build tools, the libraries and Qt. Then, in a new x64 Native Tools Command Prompt for VS 2022:

    > cmake --preset windows
    > cmake --build --preset windows
    > cmake --install build

The programs are installed to `C:\ns3\bin`, which is added to your PATH.

The full guide, with the Intel GPU build, WSL2 and troubleshooting, is `doc/INSTALL.md` in the source tree.

