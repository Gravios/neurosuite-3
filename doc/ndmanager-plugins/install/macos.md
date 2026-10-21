# ndmanager-plugins — macOS Installation

The plugins are built and installed with the rest of neurosuite-3: follow
**[Installing neurosuite-3 → macOS](../../INSTALL.md#macos)**. macOS is not yet
checked in CI; report what fails. The notes below are specific to the
plugins.

## Packages

`-DNS_INSTALL_DEPS=ON` installs the build dependencies with Homebrew. To
install the ones the plugins need yourself:

```bash
brew install cmake ninja pkg-config libxml2 yaml-cpp gsl libomp bash python
brew install ffmpeg      # optional: process_extractleds
brew install hdf5        # optional: process_aomconvert (ndm_aom2dat)
```

GSL is required (`process_pca`, `process_pca_multichannel`). Apple's compiler
has no OpenMP of its own; if the configure output says OpenMP was not found,
add `-DCMAKE_PREFIX_PATH="$(brew --prefix libomp)"`.

## bash 4 or newer

The `ndm_*` steps need bash 4 or newer; macOS ships bash 3.2 as `/bin/bash`.
Install a newer one with Homebrew (above). The scripts start with
`#!/usr/bin/env bash`, so they run with the first bash on `PATH`: check that
`bash --version` says 4 or newer in the shell you start them (or NDManager)
from. Homebrew's shell setup, `eval "$(brew shellenv)"` in `~/.zprofile`, puts
its `bin` before `/bin`. A step started with bash 3.2 stops at once with a
message saying so.

## Python packages

Homebrew's Python refuses `pip3 install` into its own environment (PEP 668).
Use a virtual environment and keep it active when you run the steps, since the
scripts call `python3` from `PATH`:

```bash
python3 -m venv ~/.venvs/ns3
source ~/.venvs/ns3/bin/activate
pip install pyyaml numpy scipy
pip install scikit-learn joblib    # optional: ndm_autoklusta
```

Check with `python3 -c "import yaml, numpy, scipy; print('ok')"`.

## CUDA

CUDA is not available on macOS. All processing programs use their CPU
(OpenMP) path.

## Verify

```bash
command -v ndm_start ndm_hipass ndm_pca process_butterworth process_pca
```

The processing programs have no `--version` option; run one without
arguments to see its usage.
