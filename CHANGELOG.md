# Changelog — neurosuite-3

Top-level changelog for this fork, consolidating every change relative
to the original Neurosuite toolchain.  Entries are grouped by date from
most recent at top.  Deep per-topic technical notes live in
`doc/design/<topic>.md`, indexed at the end.

Each entry is folded — click **Details** to expand it. Newest first.

| Date | Change |
|---|---|
| 2026-10-19 | [Docs: one installation guide for every platform](#2026-10-19--docs-one-installation-guide-for-every-platform) |
| 2026-10-19 | [Windows: OpenMP code that MSVC accepts; the CI build gets further](#2026-10-19--windows-openmp-code-that-msvc-accepts-the-ci-build-gets-further) |
| 2026-10-19 | [Windows: the ndm_* steps run from cmd and PowerShell through Git Bash](#2026-10-19--windows-the-ndm_-steps-run-from-cmd-and-powershell-through-git-bash) |
| 2026-10-19 | [Windows: the install puts the programs on PATH](#2026-10-19--windows-the-install-puts-the-programs-on-path) |
| 2026-10-19 | [Windows: one script installs the build dependencies](#2026-10-19--windows-one-script-installs-the-build-dependencies) |
| 2026-10-19 | [Windows: the ndmanager-plugins programs build natively, and CI checks it](#2026-10-19--windows-the-ndmanager-plugins-programs-build-natively-and-ci-checks-it) |
| 2026-10-19 | [Windows: `cmake --install` gives a folder that runs](#2026-10-19--windows-cmake---install-gives-a-folder-that-runs) |
| 2026-10-19 | [CI: GitHub actions moved to their Node.js 24 releases](#2026-10-19--ci-github-actions-moved-to-their-nodejs-24-releases) |
| 2026-10-19 | [Windows: fixes from the first Windows configure](#2026-10-19--windows-fixes-from-the-first-windows-configure) |
| 2026-10-18 | [Klusters: the Intel GPU (SYCL) backend can be configured with icx on Windows](#2026-10-18--klusters-the-intel-gpu-sycl-backend-can-be-configured-with-icx-on-windows) |
| 2026-10-18 | [Windows: the GUI programs build with MSVC, and CI checks it](#2026-10-18--windows-the-gui-programs-build-with-msvc-and-ci-checks-it) |
| 2026-10-18 | [CI builds and tests on Ubuntu 24.04, 25.10, 26.04 and 26.10](#2026-10-18--ci-builds-and-tests-on-ubuntu-2404-2510-2604-and-2610) |
| 2026-10-18 | [CI installs SciPy; the plugins' SciPy dependency documented as it is](#2026-10-18--ci-installs-scipy-the-plugins-scipy-dependency-documented-as-it-is) |
| 2026-10-18 | [CI builds again: two libklustersshared tests missed the export header](#2026-10-18--ci-builds-again-two-libklustersshared-tests-missed-the-export-header) |
| 2026-10-18 | [Docs: roadmap and collision-decomposition workflow brought up to date](#2026-10-18--docs-roadmap-and-collision-decomposition-workflow-brought-up-to-date) |
| 2026-10-18 | [Klusters: one table for action icons](#2026-10-18--klusters-one-table-for-action-icons) |
| 2026-10-18 | [Klusters: edit the radial menu in Preferences](#2026-10-18--klusters-edit-the-radial-menu-in-preferences) |
| 2026-10-17 | [Klusters: radial context menu (proof of concept)](#2026-10-17--klusters-radial-context-menu-proof-of-concept) |
| 2026-10-17 | [Spike extraction: one .spk row per .res time; shared extractor code](#2026-10-17--spike-extraction-one-spk-row-per-res-time-shared-extractor-code) |
| 2026-10-17 | [Reference files for the template-library formats; one copy of the extractor helpers; plugins configure on their own](#2026-10-17--reference-files-for-the-template-library-formats-one-copy-of-the-extractor-helpers-plugins-configure-on-their-own) |
| 2026-10-17 | [Core fixes: long tokens and groups, resolveAny order, int16 saturation, deterministic t-SNE; old resolver removed](#2026-10-17--core-fixes-long-tokens-and-groups-resolveany-order-int16-saturation-deterministic-t-sne-old-resolver-removed) |
| 2026-10-17 | [kiloklustakwik: heap overflow and -PriorFile fixes; KK_chunked.cpp split; driver comparison](#2026-10-17--kiloklustakwik-heap-overflow-and--priorfile-fixes-kk_chunkedcpp-split-driver-comparison) |
| 2026-10-17 | [Qt-free code moves into libneurosuite-core, with tests; merge ranking fix](#2026-10-17--qt-free-code-moves-into-libneurosuite-core-with-tests-merge-ranking-fix) |
| 2026-10-17 | [neurosuite-3 3.1.0: a version contract with fiber-kit and ndm plugins](#2026-10-17--neurosuite-3-310-a-version-contract-with-fiber-kit-and-ndm-plugins) |
| 2026-10-17 | [Recipes and canonical references; process_pca records the right input channel count; Qt build fix](#2026-10-17--recipes-and-canonical-references-process_pca-records-the-right-input-channel-count-qt-build-fix) |
| 2026-10-16 | [One stderiv transform, one token rule: shared tables for both](#2026-10-16--one-stderiv-transform-one-token-rule-shared-tables-for-both) |
| 2026-10-16 | [CI runs the tests and a lint job; HDF5 1.10 build fix; generated files removed](#2026-10-16--ci-runs-the-tests-and-a-lint-job-hdf5-110-build-fix-generated-files-removed) |
| 2026-10-16 | [Build and docs: one C++ standard, standard test layout, how to run the tests](#2026-10-16--build-and-docs-one-c-standard-standard-test-layout-how-to-run-the-tests) |
| 2026-10-16 | [Spike detection: sdiffPairs as arithmetic expressions (stderiv_C6)](#2026-10-16--spike-detection-sdiffpairs-as-arithmetic-expressions-stderiv_c6) |
| 2026-10-16 | [ndManager: export a session as a template, apply a template to a session](#2026-10-16--ndmanager-export-a-session-as-a-template-apply-a-template-to-a-session) |
| 2026-10-16 | [ndManager: parameter tables by keyboard, typed values; plugins listed by purpose](#2026-10-16--ndmanager-parameter-tables-by-keyboard-typed-values-plugins-listed-by-purpose) |
| 2026-10-16 | [Units: brain region, division, atlas and region ID](#2026-10-16--units-brain-region-division-atlas-and-region-id) |
| 2026-10-16 | [Klusters: saves are all-or-nothing and skip unchanged files; autosave covers hierarchical sessions; editing locked during a save](#2026-10-16--klusters-saves-are-all-or-nothing-and-skip-unchanged-files-autosave-covers-hierarchical-sessions-editing-locked-during-a-save) |
| 2026-10-16 | [Klusters: save-system fixes; Promote to Hierarchical Session](#2026-10-16--klusters-save-system-fixes-promote-to-hierarchical-session) |
| 2026-10-16 | [ndManager: audit fixes — saves, Pipeline tab, Probes tab, Probe Maker](#2026-10-16--ndmanager-audit-fixes--saves-pipeline-tab-probes-tab-probe-maker) |
| 2026-10-15 | [ndManager: New from Project Template; saves keep comments, inline lists and quoting](#2026-10-15--ndmanager-new-from-project-template-saves-keep-comments-inline-lists-and-quoting) |
| 2026-10-15 | [Audit fixes: ndmanager keeps unknown YAML keys; aligners and re-extraction follow the .spk convention; re-extraction threshold base](#2026-10-15--audit-fixes-ndmanager-keeps-unknown-yaml-keys-aligners-and-re-extraction-follow-the-spk-convention-re-extraction-threshold-base) |
| 2026-10-15 | [Klusters: t-SNE of the oblique coordinates](#2026-10-15--klusters-t-sne-of-the-oblique-coordinates) |
| 2026-10-14 | [ndManager: empty program parameters are no longer written as `~`](#2026-10-14--ndmanager-empty-program-parameters-are-no-longer-written-as-) |
| 2026-10-14 | [ndm_detectspikes (stderiv): refuse one-channel groups and flat signals](#2026-10-14--ndm_detectspikes-stderiv-refuse-one-channel-groups-and-flat-signals) |
| 2026-10-14 | [Klusters: crash on opening a session whose probe has no .probe file; leftover .pending copies](#2026-10-14--klusters-crash-on-opening-a-session-whose-probe-has-no-probe-file-leftover-pending-copies) |
| 2026-10-14 | [Klusters: watershed split on the noise cluster](#2026-10-14--klusters-watershed-split-on-the-noise-cluster) |
| 2026-10-14 | [Klusters: multi-lasso tool; zoom and pan in the t-SNE / oblique view](#2026-10-14--klusters-multi-lasso-tool-zoom-and-pan-in-the-t-sne--oblique-view) |
| 2026-10-12 | [Klusters: Shift+H density heatmap over the feature view](#2026-10-12--klusters-shifth-density-heatmap-over-the-feature-view) |
| 2026-10-12 | [NeuroScope: closing a session no longer deletes or overwrites parameter-file values](#2026-10-12--neuroscope-closing-a-session-no-longer-deletes-or-overwrites-parameter-file-values) |
| 2026-10-12 | [ndm_plx2dat: Plexon .plx / .pl2 → .dat + .evt with header metadata in the session YAML](#2026-10-12--ndm_plx2dat-plexon-plx--pl2--dat--evt-with-header-metadata-in-the-session-yaml) |
| 2026-10-12 | [Klusters: sync other feature spaces to the current spike times](#2026-10-12--klusters-sync-other-feature-spaces-to-the-current-spike-times) |
| 2026-10-11 | [Klusters: view one .clu through another method's feature space; spikes never change order](#2026-10-11--klusters-view-one-clu-through-another-methods-feature-space-spikes-never-change-order) |
| 2026-10-10 | [Input-binding overhaul completed across Klusters and NeuroScope; Qt 6.4–6.12 compatibility; documentation](#2026-10-10--input-binding-overhaul-completed-across-klusters-and-neuroscope-qt-64612-compatibility-documentation) |
| 2026-10-09 | [NeuroScope: two silently dead shortcuts fixed](#2026-10-09--neuroscope-two-silently-dead-shortcuts-fixed) |
| 2026-10-08 | [ndm_hipass: zero-phase Butterworth filter (new default)](#2026-10-08--ndm_hipass-zero-phase-butterworth-filter-new-default) |
| 2026-10-08 | [ndm_extractspikes filter: check the raw source window](#2026-10-08--ndm_extractspikes-filter-check-the-raw-source-window) |
| 2026-10-08 | [ndm_pca: a `_D` run adds the lag space to existing features](#2026-10-08--ndm_pca-a-_d-run-adds-the-lag-space-to-existing-features) |
| 2026-10-08 | [ndm_extractspikes: post-extraction flat-run / duplicate filter](#2026-10-08--ndm_extractspikes-post-extraction-flat-run--duplicate-filter) |
| 2026-10-08 | [ndm_detectspikes (stderiv): flat-run rejection and duplicate removal](#2026-10-08--ndm_detectspikes-stderiv-flat-run-rejection-and-duplicate-removal) |
| 2026-10-08 | [custody: `.spk`/`.res` of a `_D` feature-space token resolve to its waveforms](#2026-10-08--custody-spkres-of-a-_d-feature-space-token-resolve-to-its-waveforms) |
| 2026-10-08 | [ndm_pca: lagged feature space (`_D<lag><dims>`)](#2026-10-08--ndm_pca-lagged-feature-space-_dlagdims) |
| 2026-10-08 | [stderiv detection: threshold noise base, re-centring, hybrid arm](#2026-10-08--stderiv-detection-threshold-noise-base-re-centring-hybrid-arm) |
| 2026-10-08 | [Klusters: rebindable keyboard + mouse, and the Overview interaction redesign](#2026-10-08--klusters-rebindable-keyboard--mouse-and-the-overview-interaction-redesign) |
| 2026-10-06 | [Klusters: paint-structure refactor](#2026-10-06--klusters-paint-structure-refactor) |
| 2026-10-05 | [Klusters: template curation in the feature space](#2026-10-05--klusters-template-curation-in-the-feature-space) |
| 2026-06-08 | [Klusters high-cluster-count rendering perf, PCA-Center Align All profiling + setup hoist, cross-cluster GPU-batch design](#2026-06-08--klusters-high-cluster-count-rendering-perf-pca-center-align-all-profiling--setup-hoist-cross-cluster-gpu-batch-design) |
| 2026-05-16 | [Cluster-mean alignment stack + .res/.spk/.fet consistency](#2026-05-16--cluster-mean-alignment-stack--resspkfet-consistency) |
| 2026-04-27 | [ndm_start root + `.ndm.<n>.pipeline` files + empty-graph default](#2026-04-27--ndm_start-root--ndmnpipeline-files--empty-graph-default) |
| 2026-04-27 | [ndm_start as the graph root (replaced)](#2026-04-27--ndm_start-as-the-graph-root-replaced) |
| 2026-04-26 | [graphical Pipeline tab in ndmanager (superseded)](#2026-04-26--graphical-pipeline-tab-in-ndmanager-superseded) |
| 2026-04-26 | [graphical Pipeline tab in ndmanager (original)](#2026-04-26--graphical-pipeline-tab-in-ndmanager-original) |
| 2026-04-25 | [per-probe empirical KlustaKwik priors](#2026-04-25--per-probe-empirical-klustakwik-priors) |
| 2026-04-22 | [process_shadowcluster inflation guard + dedicated KK tier for sub-clustering](#2026-04-22--process_shadowcluster-inflation-guard--dedicated-kk-tier-for-sub-clustering) |
| 2026-04-22 | [process_reextractspikes_stderiv writes transformed .spkD (correctness)](#2026-04-22--process_reextractspikes_stderiv-writes-transformed-spkd-correctness) |
| 2026-04-22 | [fetD completion, klusters focus fixes, Pipeline C nudge correctness](#2026-04-22--fetd-completion-klusters-focus-fixes-pipeline-c-nudge-correctness) |
| earlier | [KlustaKwik](#earlier-changes--klustakwik) |
| earlier | [ndmanager-plugins](#earlier-changes--ndmanager-plugins) |
| earlier | [jg05-20120316.yaml (sample session)](#earlier-changes--jg05-20120316yaml-sample-session) |

---

## 2026-10-19 — Docs: one installation guide for every platform

<details>
<summary>Details</summary>

- New `doc/INSTALL.md`: installing neurosuite-3 on Linux, Windows (the
  install-deps script, the `windows` / `windows-sycl` presets, PATH, the
  `ndm_*` launchers, WSL2 for KiloKlustaKwik, updating, uninstalling, a
  troubleshooting table) and macOS, with the status of each platform.
- The Klusters, NeuroScope and NDManager install pages (Linux, macOS,
  Windows) described building each program on its own after
  libklustersshared. That no longer worked (libneurosuite-core was never
  built) and is not how the project is built. They now point to INSTALL.md,
  keep the program's own notes and give the skip options for building one
  program. Two stale claims went with them: NeuroScope has no FFmpeg option,
  and no `.app` bundle or `uninstall` target is made.
- ndmanager-plugins' Windows guide leads with the native build and the `.bat`
  launchers; the KiloKlustaKwik Windows guides say it is not yet part of the
  Windows build.
- The handbook's Installation chapter (Qt 4.8, sourceforge, `make`) now
  describes the current build; `doc/site/gen.sh` regenerated
  `07-installation.md` (only that page changed). README, `doc/README.md` and
  BUILD.md link to INSTALL.md.

</details>

## 2026-10-19 — Windows: OpenMP code that MSVC accepts; the CI build gets further

<details>
<summary>Details</summary>

- The `windows` CI job (MSVC `cl`, which implements OpenMP 2.0) stopped on
  OpenMP constructs from later versions; Intel `icx` and GCC accept them,
  which is why local `icx` builds succeeded. Fixed where the log showed them:
  - Klusters' template, residual and drift matrix threads and the grouping
    assistant named data members in `default(none)` clauses (C3028). With
    MSVC they take a clause-free `parallel for`: everything is shared, as the
    members were, and the per-thread copies were only of `const` locals.
    Other compilers keep `default(none)`.
  - `klusters_sort`: the degree maximum used `reduction(max)` (C7660); it is
    now taken after the parallel loop, for every compiler.
  - The spectral engine's `#pragma omp simd` (C7660) is skipped for MSVC.
  - `process_extractemg` called `omp_set_max_active_levels` (OpenMP 3.0); with
    OpenMP 2.0 it calls `omp_set_nested(0)`.
- Checked on Linux: full build and tests, and the same with the MSVC branches
  forced on under GCC. The CI log showed these errors only up to the point
  where the build stopped (685 of 792 steps), so the next run may show more.

</details>

## 2026-10-19 — Windows: the ndm_* steps run from cmd and PowerShell through Git Bash

<details>
<summary>Details</summary>

- Typing `ndm_hipass` in PowerShell opened Windows' "Select an app" dialog:
  the steps are extension-less bash scripts. A Windows install now puts a
  `.bat` launcher beside each script (`src/ndmanager-plugins/cmake/NdmWindowsLaunchers.cmake`)
  that runs it with Git for Windows' bash, or with Python for the
  `#!/usr/bin/env python3` ones (`ndm_xml2yaml`, `process_mergespikes`, ...).
  The sourced libraries (`ndm_functions`, `ndm_custody`) get none, nor does
  `process_spikegrouper`, which has an `.exe` of the same name.
- `ndm-env.bat` finds bash (`NS_BASH`, then `Program Files\Git`, the per-user
  Git, or `..\bin\bash.exe` beside `git.exe`, never WSL's `System32\bash.exe`)
  and Python (`NS_PYTHON`, `py -3`, then `python.exe` unless it is the
  Microsoft Store placeholder), and puts the bin directory and `bin\ndm-bash`
  first on PATH. `bin\ndm-bash\python3` gives the scripts the `python3` they
  call, which the Windows Python installer does not provide.
- The script is passed as `C:/.../ndm_hipass`: the scripts name themselves
  with `basename $0`, which splits on `/` only, and read their parameters by
  that name.
- The scripts are installed with LF line endings: Git for Windows checks text
  out as CRLF by default, and bash stops on the `\r`.
- Checked under Wine's cmd with stand-in bash / Python programs: arguments
  (including a quoted one with a space) arrive intact, the script path has
  forward slashes, each detection order picks the expected program, the Store
  placeholder is skipped, and a missing bash is reported. Not yet run with
  real Git Bash, so the scripts themselves are untested on Windows.

</details>

## 2026-10-19 — Windows: the install puts the programs on PATH

<details>
<summary>Details</summary>

- `cmake --install` on Windows now adds `<prefix>\bin` to the current user's
  PATH, so Klusters, NeuroScope, NDManager and the `process_*` programs start
  from any prompt opened afterwards. `cmake/NsWindowsPath.cmake` runs
  `scripts/windows-path.ps1 -Add` after the DLL deploy step.
- The script edits `HKCU\Environment` only (no administrator rights), keeps
  PATH a `REG_EXPAND_SZ` with its `%VARIABLES%` unexpanded, and records the
  directory in `NEUROSUITE3_BIN`: installing again to the same prefix changes
  nothing, to another prefix replaces the old entry. `-Remove` takes it off,
  `-Show` reports it. Off with `-DNS_ADD_TO_PATH=OFF`; skipped for a staged
  install (`DESTDIR`); a failure is a warning, never an install failure.
- Checked: the PATH editing logic against nine cases (append, already
  present in another spelling, moved prefix, removal, empty entries) in
  PowerShell 7; the install step with a Windows (MinGW) toolchain: run on
  install, skipped with `DESTDIR` and with the option off, and a failing
  script only warns. The registry write itself has not run on Windows yet.

</details>

## 2026-10-19 — Windows: one script installs the build dependencies

<details>
<summary>Details</summary>

- `scripts\install-deps-windows.bat` (a wrapper for
  `scripts/install-deps-windows.ps1`, Windows PowerShell 5.1 or later) sets
  up a Windows machine from nothing, skipping what is already installed: Git
  and the Visual Studio 2022 Build Tools with the C++ workload (winget), Python
  when Qt has to be downloaded, vcpkg with yaml-cpp, GSL, libxml2 and
  libsamplerate (HDF5 and FFmpeg on request), and Qt 6.8.3 for MSVC through
  aqtinstall, with no Qt account. It writes a git-ignored
  `CMakeUserPresets.json`: `cmake --preset windows` (MSVC) or
  `windows-sycl` (Intel `icx`), installing to `C:\ns3`. `-DryRun` prints
  what it would do. The vcpkg inside Visual Studio (`VC\vcpkg`, the
  `VCPKG_ROOT` of its developer prompts) cannot install packages outside a
  `vcpkg.json` manifest, so it is passed over for `C:\vcpkg`.
- `-DNS_INSTALL_DEPS=ON` on Windows runs the same script for the libraries and
  Qt. Before, it set `CMAKE_TOOLCHAIN_FILE` after `project()`, where it has no
  effect, so the vcpkg packages it installed were not found; it now puts the
  vcpkg tree and Qt on `CMAKE_PREFIX_PATH`. It no longer builds FFmpeg by
  default (`-DNS_DEPS_FFMPEG=ON`, `-DNS_DEPS_HDF5=ON`) and installs Qt only
  when CMake finds none (`NS_QT_ROOT`, `NS_QT_VERSION`).
- Checked: PowerShell 7 parses the script; dry runs, and runs against stand-in
  vcpkg and Qt directories (including a failing vcpkg, which stops it with the
  exit code), take the expected steps; CMake lists the generated presets; the
  CMake branch, configured with a Windows (MinGW) toolchain against the
  stand-ins, ran the script and set the prefix path. Not yet run on Windows.

</details>

## 2026-10-19 — Windows: the ndmanager-plugins programs build natively, and CI checks it

<details>
<summary>Details</summary>

- The plugins' C++ programs used POSIX calls with no Windows equivalent and
  were skipped in Windows builds. They now build with MSVC; the `ndm_*`
  steps are bash scripts and still run under WSL2 (Git Bash is the next
  step).
- libneurosuite-core gains the portable pieces the programs share:
  `MappedFile` (`mmap`, or the Windows file mapping), `fileSize64()`,
  `fileops.hpp` (`replaceFile`, which replaces an existing file as
  `rename` does on POSIX; `syncFile`; `processId`) and `strsep.hpp`.
- In the programs: 64-bit file positions through `fileio64.hpp`; `MappedFile`
  and stdio instead of `mmap` / `pread` / `stat`; binary-mode streams for raw
  data (text mode on Windows rewrites `\n` and stops at 0x1A, so
  `process_extractchannels` wrote an empty file); no glibc `strdupa`,
  `M_PI` or `libm` on Windows; no OpenMP 3.1 `reduction(max)` and no
  variable-length array, which MSVC rejects. Klusters' saves use
  `replaceFile` too.
- `process_resample` compiles its bundled libsamplerate with CMake on
  Windows (or takes vcpkg's); `process_decomposecollisions` installs a copy
  instead of a symbolic link there; `process_extractemg` takes its CPU path
  in a Windows SYCL build; `cmake --install` also copies the plugins' vcpkg
  DLLs (GSL, libxml2, yaml-cpp).
- CI: the `windows` job builds the plugins (vcpkg GSL and libxml2 added;
  HDF5 and FFmpeg left out, so `process_aomconvert` and
  `process_extractleds` are skipped).
- Checked on Linux (Ubuntu 24.04): full build and tests pass, and the
  outputs of `process_butterworth`, `extractspikes`, `reextractspikes`,
  `reextractspikes_stderiv`, `extractemg` and `alignspikes` on a synthetic
  session are byte-identical before and after. MinGW-w64 builds of the same
  programs, and of `process_resample`, run under Wine give the same bytes as
  Linux. Not yet built with MSVC outside CI.

</details>

## 2026-10-19 — Windows: `cmake --install` gives a folder that runs

<details>
<summary>Details</summary>

- On Windows the installed Klusters, NeuroScope and NDManager did not start:
  the install put the programs and the project's two DLLs in `bin`, but not
  Qt, yaml-cpp or the oneAPI runtime. `cmake --install` now also runs Qt's
  `windeployqt` on the installed programs, copies the other DLLs they link
  (vcpkg's yaml-cpp; with `icx`, the oneAPI runtime) and, for a SYCL build,
  the SYCL runtime adapters (`ur_*.dll`), which are loaded by name and found
  by no dependency scan.
- `cmake/NsWindowsDeploy.cmake` (configure time) and
  `NsWindowsDeployInstall.cmake` (install time). Windows only; off with
  `-DNS_DEPLOY_RUNTIME=OFF`. A missing tool or DLL is a warning, never an
  install failure.
- Checked on Linux: configure unchanged (the step is not added); the
  install-time script, run in script mode on the Linux Klusters and NeuroScope
  binaries, copied their libraries from the given directories and the adapter
  files, skipped the program not installed, and warned for the missing
  `windeployqt`. Not yet run on Windows.

</details>

## 2026-10-19 — CI: GitHub actions moved to their Node.js 24 releases

<details>
<summary>Details</summary>

- GitHub forces Node.js 20 actions onto Node.js 24 and warns. `ci.yml` and
  `docs.yml` now use `actions/checkout@v7`, `actions/setup-python@v7`,
  `actions/configure-pages@v6`, `actions/upload-pages-artifact@v5` and
  `actions/deploy-pages@v5` (all Node.js 24 or composite; inputs and
  permissions unchanged). `ilammy/msvc-dev-cmd@v1` has no Node.js 24 release
  yet and stays.
- The docs deploy still needs GitHub Pages turned on for the repository
  (Settings ▸ Pages ▸ Source: GitHub Actions); no workflow change can do that
  with the default token.

</details>

## 2026-10-19 — Windows: fixes from the first Windows configure

<details>
<summary>Details</summary>

The first configure on Windows (MSVC environment, oneAPI 2025.1 `icx`, Qt 6.8,
vcpkg) stopped on two errors:

- `Cannot find source file: WIN32` for NeuroScope and NDManager: their
  `add_executable` passed `WIN32` through a generator expression, which CMake
  reads as a file name. Both now use the plain keyword, like Klusters.
- Intel's own `IntelSYCLConfig.cmake` in oneAPI 2025.x fails on Windows
  (`separate_arguments` / `SYCL_FEATURE_TEST_EXTRACT`; a known oneAPI bug).
  On Windows Klusters no longer loads Intel's IntelSYCL / IntelDPCPP packages;
  it already sets `-fsycl` itself, and now links the SYCL runtime import
  library (`sycl8.lib`, or `sycl7.lib` for 2024) directly.
- The first Windows build then failed in three Klusters sources inside Qt's
  `qmetatype.h` ("constexpr variable 'begin' must be initialized by a
  constant expression", from `qvariant_cast<QIcon>` in `qtablewidget.h` /
  `qlistwidget.h`). `-fsycl` was on every Klusters source, so each got a SYCL
  device pass that re-reads Qt's headers, where Qt's `__FUNCSIG__`-based
  type-name parsing fails. On Windows `-fsycl` now applies only to the two
  SYCL sources (`groupingassistant_sycl.cpp`, `realign_xcorr_sycl.cpp`, both
  Qt-free); the link keeps `-fsycl` and, with Ninja, runs through `icx`.

- The next build stopped on two more Windows-only gaps. NDManager linked
  `yaml-cpp` without finding the package itself (it now does), so Windows
  looked for a bare `yaml-cpp.lib`. NeuroScope called glibc's `fseeko64`,
  and Klusters `fseeko` / `ftello` with `off_t` offsets: MSVC has none of
  these, and its `off_t` is 32-bit. A new header,
  `neurosuite/core/fileio64.hpp`, gives `fseek64` / `ftell64` with
  `std::int64_t` offsets (`fseeko` / `ftello` on POSIX, `_fseeki64` /
  `_ftelli64` on Windows); Klusters and NeuroScope use it throughout.

Linux configures and builds as before.

</details>

## 2026-10-18 — Klusters: the Intel GPU (SYCL) backend can be configured with icx on Windows

<details>
<summary>Details</summary>

- Klusters' SYCL setup recognised the oneAPI compiler only by the name
  `icpx`, and otherwise switched the cached compiler to `icpx` part-way
  through configuring. On Windows the oneAPI compiler for an MSVC/Qt build is
  `icx`, so SYCL could not be enabled there. It now goes by the compiler id
  (`IntelLLVM`, covering `icx` and `icpx`); on Windows it never switches the
  compiler and says to configure with `-DCMAKE_C_COMPILER=icx
  -DCMAKE_CXX_COMPILER=icx`. Linux behaves as before.
- `ZenOptimizations.cmake` passes `-funroll-loops` / `-march=native`, and
  overrides the archiver with `llvm-ar`, only for GNU-style drivers, not for
  `icx`'s MSVC-style command line. GCC builds are unchanged.
- Not built here (no oneAPI on Windows available); checked that a GCC
  configure on Ubuntu 24.04 produces the same flags as before.

</details>

## 2026-10-18 — Windows: the GUI programs build with MSVC, and CI checks it

<details>
<summary>Details</summary>

- The Windows guides were never checked by CI, and the GUI code no longer
  compiled with MSVC. Removed: Klusters' unused `gettimeofday` timer
  (NeuroScope's runs on `std::chrono::steady_clock`); four unused
  `<unistd.h>` includes; `M_PI` (now `std::numbers::pi`, bit-identical).
- Ported: Klusters' save path (file stamps, flushes, `markInSync`'s mtime)
  and libklustersshared's terminal progress bar have Windows branches; the
  POSIX code is unchanged. `filecommit_test` now checks a stamp's fields.
- New CI job `windows`: Klusters, NeuroScope, NDManager and their libraries
  with MSVC 2022, Qt 6.8 and vcpkg's yaml-cpp; build only, non-blocking until
  its first green run. KiloKlustaKwik and ndmanager-plugins stay POSIX-only
  (WSL2 on Windows).
- Checked: full Linux build and tests on Ubuntu 24.04 (Qt 6.4); the Windows
  branches cross-compiled with MinGW-w64 and run under Wine (file-commit and
  hierarchy tests pass; a deliberately broken stamp fails). MSVC itself has
  not compiled this yet; the new job's first run will.

</details>

## 2026-10-18 — CI builds and tests on Ubuntu 24.04, 25.10, 26.04 and 26.10

<details>
<summary>Details</summary>

- The `build` job runs once per Ubuntu release, each in that release's
  `ubuntu:<release>` container with its own distribution packages: 24.04 (the
  reference, Qt 6.4), 25.10 (Qt 6.9), 26.04 LTS (Qt 6.10) and 26.10 (Qt 6.11).
  The newer releases bring GCC 15, CMake 4, Python 3.13/3.14 and NumPy 2.
- 25.10 reached end of life on 2026-07-09. Its job does not fail the run, and
  its apt sources fall back to old-releases.ubuntu.com once the archive moves.
- Checked before committing by running the job's steps in a root filesystem of
  each release, as root like the containers: 24.04, 26.04 and 26.10 build and
  pass all 82 tests (`kk_test_per_channel_split` is disabled) with no code
  changes.
- BUILD.md §1 and §8, CONTRIBUTING.md and the roadmap describe the matrix; Qt
  6.12 still has no job.

</details>

## 2026-10-18 — CI installs SciPy; the plugins' SciPy dependency documented as it is

<details>
<summary>Details</summary>

- With the build fixed, CI's test step failed on `ndm_test_adaptmodel`: the
  waveform-adaptation fit (`adaptmodel.py`, used by `process_reassignspikes`)
  imports `scipy.optimize` for its selftest, and the CI job installed only
  NumPy and PyYAML. CI now installs `python3-scipy`.
- The dependency table called SciPy optional, "sub-sample refinement in
  `ndm_estimatedrift`". `ndm_spikegrouper`, `ndm_autoklusta` and the adaptation
  fit need it; `ndm_estimatedrift`, `ndm_localise` and `process_subtractspikes`
  use it with a fallback. The table says so, and the Linux, macOS and Windows
  install guides now install NumPy and SciPy (they listed only PyYAML).
- Checked in a Python matching the runner (3.12.3, NumPy 1.26.4, SciPy 1.11.4,
  PyYAML 6.0.1): all 20 ndmanager-plugins tests pass; with SciPy hidden,
  `ndm_test_adaptmodel` fails exactly as in CI.

</details>

## 2026-10-18 — CI builds again: two libklustersshared tests missed the export header

<details>
<summary>Details</summary>

- `klustersshared_yamlformat_test` and `klustersshared_sessiontemplate_test`
  compile `yamlformat.cpp` / `sessiontemplate.cpp` directly, and their headers
  include `libklustersshared_export.h` (since 2026-10-15 / 10-16). That header
  is in `src/libklustersshared/src/`, which neither test had on its include
  path, so every CI build stopped there (the tests are built only with
  `NS_BUILD_TESTS=ON`, as CI does). Both now include `src/`.
- Reproduced the CI job locally (ubuntu-24.04, Qt 6.4.2, its package list,
  configure flags and fiber-kit checkout): before, the whole tree built except
  these two targets; after, it builds and `ctest` passes (82 tests, one
  disabled in the repository).

</details>

## 2026-10-18 — Docs: roadmap and collision-decomposition workflow brought up to date

<details>
<summary>Details</summary>

- `doc/workflows/collision-decomposition.md` described the pre-Klusters state:
  file names in the retired `.col.N` form, a `ndm_decomposecollisions --commit`
  flag that does not exist, viewer keys I / D as "info" / "delete" (they scale
  the waveforms), and viewer decisions as if they fed back into the `.col`
  (they go to `SESSION_decisions.csv`). It now describes both routes — *Plugins ▸
  process_decomposecollisions* in Klusters for the open group, and the shell
  command for every group — and step 4 as Klusters' decollide: `.eap` / `.tcl`
  membership on the current stage plus a new stage carrying the decomposed
  spikes.
- `ROADMAP.md`: the "considered" entry calling `process_decomposecollisions` an
  offline tool with no GUI hookup is replaced by a Done entry (Klusters has run
  it from the Plugins menu since 2026-10-02); Done entries for the radial menu
  and the action-icon standard; the live-pass item lists the radial-menu and
  icon-theme checks still owed on a real desktop.

</details>

## 2026-10-18 — Klusters: one table for action icons

<details>
<summary>Details</summary>

- **The standard** ([STANDARDIZATION.md](doc/dev/STANDARDIZATION.md) §2.11):
  an action's icon is a row in one table keyed by the action's stable id
  (`actionid.h`), applied in one pass — no `setIcon()` where the action is
  created. Generic actions use the desktop theme's icon by freedesktop name
  with the bundled copy as fallback; Klusters' own icons load as
  `:/icons/<alias>` with lower-case, hyphenated aliases; icons painted in code
  take their outline from the palette. Recipe: [RECIPES.md](doc/dev/RECIPES.md)
  §2.6.
- Klusters: `actionicons.h/.cpp` holds the 25 icons that were set at 25
  creation sites in `klusters.cpp`; `KlustersApp::applyActionIcons()` sets them
  at start-up and again after Preferences ▸ Apply. `actionid.h` /
  `klusters_actionids.cpp` take the action ids out of the radial-menu code so
  both use the same names.
- `klusters-icons.qrc` gives every icon a descriptive alias
  (`delete_artefact_tool.png` → `delete-artifact-spikes`, `backCluster.png` →
  `previous-spike`, …); the files are unchanged.
- Fix: *File ▸ Renumber and Save* never had an icon — it pointed at
  `filesave.png` in the working directory. It now uses `document-save`.
- *File ▸ Quit* asks the theme for `application-exit` (the freedesktop name for
  quitting), falling back to the bundled `window-close` it showed before.
- The two multi-lasso icons draw their loops in the palette's text colour, so
  they stay visible in the dark theme.
- New test `klusters_test_actionicons`: every row against the icons the
  program ships (both `.qrc` files compiled in) and the drawn icons' palette
  dependence; it fails on a renamed-away alias and on a fixed outline colour
  (negative controls). A build with `-DNS3_VERBOSE=1` logs table rows whose
  action is missing and toolbar buttons without an icon.

</details>

## 2026-10-18 — Klusters: edit the radial menu in Preferences

<details>
<summary>Details</summary>

- **Preferences ▸ Radial menu** edits the right-click wheel: add any menu-bar
  action (searchable catalogue by menu, plus the X / Y feature-axis wedges),
  drag rows to reorder them or move them between groups, add / rename / remove
  groups, give actions short wedge labels, and restore the default. A preview
  below the tree shows the wheel and opens the selected group's ring. Apply or
  OK rebuilds the wheel for the next right-click.
- The wheel's tree is now data (`radiallayout.h`): JSON in the Klusters
  settings (`radialMenu/layout`; absent = the shipped default, so a later
  change to the default reaches users who never edited theirs). Actions are
  named by their input-registry id (e.g. `sort.bySnr`) or, where they have
  none, by menu path (`menu:Tools/New Cluster`); an id that no longer resolves
  is skipped and shown in red in the editor. The shipped default is the wheel
  as before (Tools, Clusters, Recluster, Undo, Redo, Hierarchy, Sort, X, Y).
- `RadialMenu` gains an embedded mode (an ordinary child widget for the
  preview: hover opens rings, nothing is picked), a ring-depth limit, and an
  open path (the rings shown when the cursor is elsewhere).
- New test `klusters_test_radiallayout`: JSON round trip, settings fallback,
  malformed / wrong-version rejection, unknown kinds skipped, ring counting,
  menu-path ids.
- Tested in the running app on Xvfb (Qt 6.4.2), synthetic session, xdotool
  input: the default wheel resolves every id; removing a group, adding an
  action from the catalogue, renaming a group, a wedge label, and a drag into
  a group all show in the preview and, after OK, in the real wheel; the added
  action triggers; the layout survives a restart; Restore Default + OK
  removes the stored key and brings the shipped wheel back.

</details>

## 2026-10-17 — Klusters: radial context menu (proof of concept)

<details>
<summary>Details</summary>

- **Right-click** in a view opens a hierarchical radial menu: the root
  actions on a ring around the cursor, and each group's actions on the next
  ring out when hovered (up to four rings). It supports press-drag-release
  (pick by direction in one motion) and click mode, and Esc backs out one ring.
- `RadialMenu` (`radialmenu.h/.cpp`) lays out any `QMenu` tree, so a wedge is
  the existing `QAction`: enabled and checked state, icons and shortcuts are
  the live ones. Hidden actions and submenus they leave empty are skipped.
- The tree (`klusters_radialmenu.cpp`) reuses menu-bar actions: Tools,
  Clusters, Recluster, Undo, Redo, Hierarchy, Sort. Sort holds the Actions ▸
  Sort Clusters entries under short wedge labels (`RadialMenu::setLabel`; the
  menu bar keeps the full names and the status bar shows them on hover).
- Two value wedges, **X: n** and **Y: n**, set the feature dimensions: the
  mouse wheel over one steps that axis (the toolbar's Features (x,y)
  spinboxes, so the scatter redraws live behind the menu). A click never picks
  them, so the menu stays open (`RadialMenu::setAdjustable` / `adjusted()`). The chord is the app-scope command `app.radialMenu`, so
  it appears in Preferences ▸ Input and Help ▸ Shortcuts and can be rebound.
  Right while drawing a lasso still removes the last vertex, and the engaged
  lineage overlay keeps its own menu. Rebound away from Right, plain
  Right-click opens the older Sort Clusters list again.
- Fix: the views' right-click filter (`KlustersView::eventFilter`) opened Sort
  Clusters for every right press and swallowed it, so the radial menu never
  appeared. It now runs any registry-bound right press itself. A right-click
  on a panel's title bar is left to Qt (its panel show/hide list); the filter
  used to open its own menu there too, so two menus came up at once.
- Tested in the running app on Xvfb (Qt 6.4.2) with a synthetic session and
  real mouse input: cluster, waveform and correlogram views, flick and click
  picks, lasso undo-vertex. Not yet tried on a real desktop or the reference
  session; the lineage-overlay case is by code reading.

</details>

## 2026-10-17 — Spike extraction: one .spk row per .res time; shared extractor code

<details>
<summary>Details</summary>

- **Fix: re-extraction (`-R`, what `ndm_extractspikes` runs) skipped spikes
  near the ends of the recording.** A `.res` time whose window was not inside
  the `.fil` got no `.spk` row, so the `.spk` was shorter than the `.res`, and
  an early skip shifted every later waveform against its spike and `.clu`
  row. Each time now gets a row; a window outside the `.fil` is zeros, with a
  warning. The three extractors' `-R` code (one mmap copy each) is replaced by
  one helper on `core/spike_extract.hpp`, the code Klusters re-extracts with;
  `SpikeWindowSpec::temporalDiff = false` gives sdiff's spatial-only
  transform. Every other `.spk` byte is unchanged (39 regression runs).
- **Fix: `process_extractspikes` read overlapping windows late.** Its
  detection run's waveform pass reads the `.fil` forward; a window starting
  before the previous one ended (two spikes closer than the waveform length,
  in any groups) was read from the current position instead, and the error
  carried on to later windows: 4.5 % of waveforms wrong at the default
  threshold on a synthetic session, nearly all of them with a low threshold.
  The `.res` was right. The pipeline discards this `.spk` (`ndm_extractspikes`
  re-cuts it), but `ndm_redetectspikes` merges it into the session's `.spk`:
  re-extract after a redetection. `ndm_test_extract_alignment` checks every
  row of the three extractors' output against its `.res` time.
- **`process_extractspikes` uses the shared peak-search and argument
  helpers** too. Fixed on the way: a trailing empty electrode group
  (`-c 0,1,2,3:`) aborted the run (a `delete[]` on stack memory); the
  "basename -" stdin form, which never worked, is refused with a message.
- `ndm_functions`: `ndm_resolve_input`, unused, is removed (`ndm_resolve` /
  `ndm_resolve_any` resolve per-group files).

</details>

## 2026-10-17 — Reference files for the template-library formats; one copy of the extractor helpers; plugins configure on their own

<details>
<summary>Details</summary>

- **Reference files for `.wti`/`.wtf`/`.wtl`/`.eap`/`.tcl`**
  (`src/libneurosuite-core/test/golden/`, shared byte for byte with
  fiber-kit, hashes in `contract.yaml`). `golden.*` are written by the C++
  writers, `fk.*` by fiber-kit's `neuro_io`. `core_test_golden_formats`
  checks that the C++ writers still produce `golden.*` and reads every file
  field by field, including fiber-kit's v1 `.wtl`; fiber-kit's
  `test_golden_formats.py` does the mirror image. RECIPES §2.1 says how to
  regenerate them.
- **Fix: `.wtl` running means and stds keep full float precision.** They were
  written with 6 significant digits, so every save rounded them
  (1234.5678 → 1234.57). Now the shortest text that reads back as the same
  float. Found by the reference-file test.
- **One copy of the extractor helpers.** `process_extractspikes_sdiff` and
  `process_extractspikes_stderiv` shared 338 identical lines (peak search,
  refractory rule, `-c` parsing, argument checks). They are now in
  `src/ndmanager-plugins/src/extractspikes_common/extractspikes_detect.h`.
  Old and new binaries give byte-identical `.res`/`.spk` in 25 runs on a
  synthetic session.
- **ndmanager-plugins configure on their own** (`cmake -S
  src/ndmanager-plugins`). The plugin CMake files assumed the monorepo root
  was the top-level project; standalone configure failed. BUILD.md §5.
- **CORE_API §3.21** documents `channel_expr.hpp` (C21).

fiber-kit 0.45.0: the same reference files, `neuro_io.read_mti` /
`read_mtf` for the committed template model, `doc/` merged into `docs/`.

</details>

## 2026-10-17 — Core fixes: long tokens and groups, resolveAny order, int16 saturation, deterministic t-SNE; old resolver removed

<details>
<summary>Details</summary>

Known issues C3–C6 and C8–C10 in `CORE_API.md` §5 are fixed; each fix has a
shared-table row or a unit test that fails on the old code.

- **Method tokens with a long order.** An `S`/`C` order longer than 9 digits
  (`stderiv_C1234567890`) makes the token opaque. The C++ parser overflowed
  `int`; C++, Python, bash and fiber-kit now agree (`custody_vectors.tsv`).
- **`parseAnchor`: long or non-ASCII groups.** A group longer than 9 digits
  threw from `std::stoi`; it is now "not an anchor" in every mirror. Python
  no longer accepts non-ASCII digits (`٣`), and bash reads `007` as 7, not as
  octal.
- **`resolveAny` picks the same copy everywhere.** With several methods'
  copies present, the first in byte order of the file name wins, not the first
  the filesystem lists. bash no longer matches a dotted middle
  (`res.x.y.5`).
- **An out-of-range `.eap` class is refused** by `template_generate` (it read
  outside the matrix).
- **Decollide output saturates to int16** instead of wrapping.
- **t-SNE gives the same embedding on any number of threads**: the
  normalising sum is added in point order.
- **One `classForUnit`** in `templateclass.hpp`; `staleAfterRealign` (and its
  Python twin) and two unused drift helpers removed.
- **`neurofileio::resolveInput` / `preferDerived` / `preferCanonical`
  removed.** Their last caller, Klusters' spike-realign dialog, reported the
  PCA basis NOT FOUND for a suffixed stderiv session (`.pca.stderiv_C5.N`)
  that the realign itself found. The dialog now resolves through custody, as
  the realign does. (Not compiled here: no Qt.)
- `contract.yaml`: the contract number is bumped only when the protocol, the
  session schema or a format changes; a table change shows in its hash.

fiber-kit 0.44.1 carries the same token bound, eap-class check and
saturation.

</details>

## 2026-10-17 — kiloklustakwik: heap overflow and -PriorFile fixes; KK_chunked.cpp split; driver comparison

<details>
<summary>Details</summary>

- **Fix: heap overflow when `MaxClusters > MaxPossibleClusters`.** Every
  per-cluster array is sized `MaxPossibleClusters`, but a run starts up to
  `MaxClusters` clusters. With `-MaxPossibleClusters` below the default
  `MaxClusters` (200), MStep wrote past its buffer and KK crashed later
  (AddressSanitizer: KK.cpp:193). KK now refuses that combination with a
  message.
- **Fix: `-PriorFile` now replaces `MaxClusters` and `MergeThresh`.** The
  prior compared them against stale copies of the defaults (10 and 30, where
  the defaults are 200 and 0 = auto). It never replaced either, and it
  overrode a user who passed exactly those values. The defaults now have one
  home (`KlustaKwik.h`).
- **`KK_chunked.cpp` split** (7,463 → 5,070 lines). `MergeChunkModels` moves
  to `KK_chunk_merge.cpp` and `RunPhase2bMode3Chunk` to
  `KK_phase2b_mode3.cpp`, verbatim. Old and new binaries give a
  byte-identical `.clu` (and `.spk` with Phase 2b mode 3) on a synthetic
  chunked session.
- **The two `RunChunkedCEM` drivers have drifted**
  ([runchunkedcem-drivers.md](doc/design/runchunkedcem-drivers.md)).
  - With `-ChunkFile` there is no preseed, the deletion floor is the
    starting K, and `-FusePerChunkStages` and
    `-AlternatingSplitCooldownIters` are ignored.
  - With `-ChunkMinutes`, `-FiberStageEnable` is ignored.
  - The two drivers run the K-template split and DipSplit in opposite
    orders.

  Merging the drivers waits on decisions and on the reference sessions.
- `ARCHITECTURE.md` is brought up to date: the `KK.cpp` split is done and
  the line counts are current.

</details>

## 2026-10-17 — Qt-free code moves into libneurosuite-core, with tests; merge ranking fix

<details>
<summary>Details</summary>

Plain C++ that lived inside the apps moves into libneurosuite-core, where it
is built and tested without Qt. All the moves are unchanged code; each was
checked bit-identical against the old copy.

- **`dipsplit.hpp`**: the bimodality gate, PCs, two-way k-means and BIC.
  kiloklustakwik and Klusters each had a copy, and the copies had drifted.
  The header is their union.
- **`watershed2d.hpp`**: Klusters' 2D density watershed.
- **`Neurosuite::spectral`**: NeuroScope's multitaper library, as a static
  library in core. Its self-test is now `core_test_spectral` and runs
  without Qt; it never did before.
- **`tsne_embed.hpp`, `oblique_embed.hpp`, `waveformiou.hpp`,
  `mergerecommend.hpp`**: Klusters' t-SNE and oblique presentations and
  its merge ranking. All four said they were "Qt-free so it can be
  unit-tested" but had no test.
- **New tests** with known answers: `core_test_dipsplit`,
  `core_test_watershed2d`, `core_test_embed` and `core_test_merge`.
- **Fix: merge recommendations.** When every candidate pair had the same
  overlap (or the same error score), that witness gave every pair rank 0.5
  instead of abstaining as documented. At the default quality floor (0.90)
  the panel then showed nothing. Now the other witness decides.
- **Found, not changed** (CORE_API §5):
  - `valley_test` scores the valley beside a lone tail sample as deep
    (C22).
  - t-SNE repeats bit-identically only on one thread (C23).
- *Klusters and NeuroScope are not compiled here (no Qt)*. Their changes
  are includes, namespace prefixes and CMake.

</details>

## 2026-10-17 — neurosuite-3 3.1.0: a version contract with fiber-kit and ndm plugins

<details>
<summary>Details</summary>

- **Releases are numbered and tagged** from here on. This is 3.1.0 (the
  root `project()` version; it was 3.0.0 and never tagged). Its tag is
  `v3.1.0`. The matching fiber-kit release is 0.44.0 (`v0.44.0`).
- **`contract.yaml`** at the repository root records what neurosuite-3
  promises at its boundaries:
  - the ndm plugin protocol (1);
  - the session-schema version (`1.0`);
  - for `.pca`, `.wti`, `.wtl`, `.tcl` and `.eap`, the highest version read
    and the version written;
  - the hashes of the tables shared with fiber-kit.

  fiber-kit keeps its own. `core_test_contract` checks the file against
  `core/version_contract.hpp` and against what the writers actually put on
  disk. `core_shared_contract` checks the table hashes on every run; with a
  fiber-kit checkout it also checks that each side reads every version the
  other writes.
- **ndmanager warns about plugin format versions.** A plugin can list
  `format <name> <read> <write>` lines in its `--ndm-version` answer
  (fiber-kit 0.44.0 does). ndmanager logs any format one side writes and the
  other cannot read, and says so in the status bar. The plugin is still
  loaded. The protocol number and the session schema written by the YAML
  layer now come from the same header. *Not compiled here (no Qt).*
- **Standalone core tests.** `cmake -S src/libneurosuite-core
  -DNS_BUILD_TESTS=ON` now registers and passes its 23 tests.
- **Docs:** [RECIPES.md](doc/dev/RECIPES.md) §2.5 (changing a version),
  CORE_API.md §3.13.

</details>

## 2026-10-17 — Recipes and canonical references; process_pca records the right input channel count; Qt build fix

<details>
<summary>Details</summary>

- **`doc/dev/RECIPES.md`.** One page that names the canonical reference
  for each file format, shared rule and schema. It also gives step-by-step
  recipes for the changes made most often:
  - a row of a shared vector table;
  - a PCAE method;
  - a session-YAML section;
  - an ndmanager plugin step.

  Each recipe lists every file to touch and the test that proves the change.
  Linked from CLAUDE.md, the Developer Guide, STANDARDIZATION.md and
  tests/README.md.
- **Drifted copies replaced by links.** `doc/libklustersshared/README.md`
  carried an older `.probe` layout and an incomplete probe-library search
  order. Both now point to `formats/probe.md` and `ndm_setupgroups.md`. Its
  schema list gains `sourceRecording`, `probeLibraryPath` and `template`.
  CORE_API.md §3.6 now lists `StderivExpr` (9) and `SdiffOrder::Expr` (6).
- **`process_pca`** wrote `nInputChannels` one short for `--pca-method 7`
  and `8` (custom pairs and sets). It now takes the dropped-channel rule
  from `custody::sdiffDropsLastChannel`. Its usage text lists method 9.
  `test_subset_pca` checks the header for methods 0 and 4–9.
- **CI** also runs when only `doc/` or `templates/` change. The doc-link and
  template checks read those directories.
- **Qt build fix.** `channel_expr.hpp` had a method named `emit`, which is a
  macro in every Qt translation unit, so Klusters failed to compile. It is
  now `push`. A new ctest, `core_test_qt_keywords`, compiles every core header
  with Qt's keyword macros defined.

</details>

## 2026-10-16 — One stderiv transform, one token rule: shared tables for both

<details>
<summary>Details</summary>

- **One spatial derivative.** The stderiv transform was written out in nine
  places:
  - four sites in the extractor;
  - the re-extractor;
  - the sdiff extractor;
  - `process_pca_stderiv`;
  - the Klusters nudge;
  - KK's all-pairs helper.

  All now call `stderiv_transform.hpp`. Its new `spatialDerivBy` reads
  channels through an accessor, so detectors read the record in place.
  Every tool's output is byte-identical before and after on the synthetic
  session, over 13 extractor configurations. The extractor is about 8%
  faster, and the re-extractor is no slower.
- **One misfit rule.** A custom map that does not fit the group gives
  all-pairs for orders 4, 5 and 6. Core used to return the raw value for
  order 4. The Klusters nudge on an order-4/5 basis without a session
  pattern now follows the engines.
- **`process_pca_stderiv -d 4`** is a pass-through flag in the code, no
  longer a value that collided with order 4 (custom). The command line is
  unchanged.
- **New shared tables**, each run on both sides:
  - `stderiv_vectors.tsv`: 34 transform cases, including saturation,
    halves and maps that do not fit;
  - `sdiff_grammar_vectors.tsv`: 39 order-4/5 patterns, also run by
    `ndm_custody`.

  `custody_vectors.tsv` gains `waveform_token` and `drops_last` rows and
  more `method_token` rows.
- **Fixed drift these tables found:**
  - The method-token parsers disagreed on unknown, repeated and malformed
    suffixes, so `stderiv_X1_D34` had waveform token `stderiv_X1` in C++
    and bash. All four parsers now make such a token wholly opaque, as
    custody.hpp documented.
  - `process_pca_stderiv`'s dropped-channel rule is now
    `custody::sdiffDropsLastChannel`.
- **STANDARDIZATION.md §2.10** lists each shared rule's single home, its
  mirrors and its table.

</details>

---

## 2026-10-16 — CI runs the tests and a lint job; HDF5 1.10 build fix; generated files removed

<details>
<summary>Details</summary>

- **CI runs the regression suite.** `ci.yml` used to build but never test.
  It now configures with `NS_BUILD_TESTS=ON`, checks out fiber-kit, and runs
  ctest.
- **More tests under ctest.**
  - The 18 ndmanager-plugins script tests are registered as `ndm_test_*`.
  - `core_shared_contract` checks that the vector tables match fiber-kit's
    (`-DNS_FIBER_KIT_ROOT`).
  - Tests that cross-check a plugin binary are given this build's binary.
- **`test_subset_pca.py`** parsed the pre-PCAE `.pca` header and crashed
  on real output. It looked for `process_pca` at a path no build produces.
  Both are fixed.
- **`process_aomconvert` built only against HDF5 1.12+.** Ubuntu 24.04,
  the documented platform and the CI image, ships HDF5 1.10, where the
  five-argument `H5Oget_info_by_name` does not exist. It now calls the
  versioned function, so it builds on both.
- **Lint.**
  - `scripts/lint.sh` checks four things, in seconds and without a build:
    `bash -n` on every shell script, ruff's defect rules on every Python
    file, no generated file tracked, and the build invariants.
  - CI runs it as the `lint` job.
- **805 generated files removed:**
  - Doxygen HTML from the original authors' machines;
  - configure output inside the vendored libsamplerate (CMake builds a
    copy and configures it there);
  - clazy compile databases.

  `.gitignore` names them. Its `install/` rule no longer hides the
  `doc/*/install/` guides.
- **fiber-kit reads `.wtl` v2** (fiber-kit 0.41.0), so FILE_FORMATS.md no
  longer lists a format divergence. On the roadmap, only the `.mti`/`.mtf`
  reader remains.

</details>

---

## 2026-10-16 — Build and docs: one C++ standard, standard test layout, how to run the tests

<details>
<summary>Details</summary>

- **C++20 everywhere.** The 34 targets that still declared `cxx_std_17` (all
  of them were already built as C++20) now declare `cxx_std_20`. The rule
  that core headers had to stay C++17-clean is dropped, since nothing
  needed it. `process_smrconvert` and libsamplerate are untouched.
- **klusters tests** moved from `src/klusters/tests/` to
  `src/klusters/test/`, like every other subproject. Test names are
  unchanged.
- **`build_invariants`** gains two checks. One is for the C++ standard: every
  root has `CMAKE_CXX_STANDARD 20` and nothing asks for another standard.
  The other is for the layout: `test/` and never `tests/`, and no sources at
  a subproject root. The `cmake -DREPO=. -P tests/cmake_invariants.cmake`
  form now works.
- **STANDARDIZATION.md** now matches the tree:
  - the syntax gate is `-std=c++20`;
  - a new §2.9 sets out the standard;
  - libneurosuite-core is in the project map;
  - the hue formula is balanced;
  - §4 describes the patch-series loop in place of the old tarball one;
  - §9 has a tests gate.
- **[tests/README.md](tests/README.md)** is the one page on running tests. It
  covers:
  - ctest flags and test-name prefixes;
  - a Qt-free configure;
  - the ndmanager-plugins script tests and the fiber-kit contract check,
    which do not run under ctest;
  - where to add a test.

</details>

---

## 2026-10-16 — Spike detection: sdiffPairs as arithmetic expressions (stderiv_C6)

<details>
<summary>Details</summary>

A spike group's `sdiffPairs` may now give one formula per output channel, in
channel order, over the group's channels `c0`, `c1`, …:

```yaml
sdiffPairs: "c0 - (c1 + c2)/2, c1 - c0, c2 - 0.5*c3 - 0.5*c1, c3 - c2"
```

- `+ - * / ^` and parentheses in the usual order of precedence (`^`
  right-associative, binding tighter than a unary minus); numbers are
  constants; any arithmetic, so the transform need not be linear. A
  non-finite result is 0, one beyond ±10⁹ is held there, and the value is
  rounded and saturated to int16 as for every order.
- Spatial order 6, method token `stderiv_C6`, PCAE method 9. Every channel
  is kept for PCA. A group whose element count or channels do not fit is an
  error naming the group.
- The older forms keep their meaning (`a-b,…` order 4, `a-b+c,…` order 5);
  in a run where some group uses expressions, the others are written as the
  expressions they compute, with the same values to the last bit.
- Applied everywhere the transform is: detection and extraction, `-R`
  re-extraction, `ndm_reextractspikes`, both aligners, and Klusters' realign,
  nudge, re-extract on save and feature-space sync. The scripts pass the
  pattern quoted, so `*` and parentheses are never globbed.
- One grammar, `neurosuite/core/channel_expr.hpp`, with cross-language
  vectors (`sdiff_expr_vectors.tsv`) that fiber-kit runs against its port.
  Checked end to end: the older forms and their expressions give
  byte-identical `.res`/`.spk`, and fiber-kit's transform reproduces the
  engine's `.spk` for a nonlinear pattern.
- Also fixed on the way: `clampToInt16` saturates before rounding (a value
  beyond the range of `int` overflowed); fiber-kit's order-4 parser left the
  root channel at 0 where the extractor writes `x[root] − x[pred]` (fiber-kit
  patch).

See `doc/ndmanager-plugins/commands/spike-detection.md` — *Expressions
(order 6)*.

</details>

## 2026-10-16 — ndManager: export a session as a template, apply a template to a session

<details>
<summary>Details</summary>

- **File ▸ Export Session as Template…** saves chosen sections of the open
  session (unsaved edits included) with a name and description to your
  template folder (`<AppDataLocation>/templates/<name>.template.yaml`) or any
  file, in the session's layout. Session-specific sections (general
  information, source recording, units) start unchecked.
- **File ▸ Apply Template to Session…** lists your templates by name (or takes
  any file) and replaces the checked sections of the open session with the
  template's, keeping the rest. Processing sections (groups, display,
  plugins, LFP rate) start checked; the recording's setup (acquisition, files,
  video, probes) and units do not. Warns when groups made for another channel
  count would be applied. The session counts as modified until saved; the
  applied sections keep the template's comments.
- A template is an ordinary parameter file plus a `template:` block; a session
  saved from one does not carry the block. The rules are in
  `libklustersshared` (`sessiontemplate.h`, tested by
  `klustersshared_sessiontemplate_test`).

See `doc/ndmanager/README.md` — *Your own templates*.

</details>

## 2026-10-16 — ndManager: parameter tables by keyboard, typed values; plugins listed by purpose

<details>
<summary>Details</summary>

- **Tab never traps the keyboard.** In every ndManager table Tab and
  Shift+Tab move cell to cell and leave the table after the last cell (or
  before the first). An invalid entry is put back to its last good value with
  the reason beside the cell, instead of holding the focus until it is fixed.
  The help field, the pipeline canvas and the parameter page's buttons are in
  the Tab order.
- **Parameter tables edit like a spreadsheet:** double-click, F2 or typing
  starts an edit; Add opens the new row's name; **Move Up / Move Down**
  reorder rows.
- **Typed parameters.** A description's optional `type` (`bool`, `int`,
  `float`, `choice:a|b|c`), `default` and `hidden` keys are kept — through the
  table, the description file and the session file's `programs:` entry —
  instead of being dropped on the first save. The Value cell's editor follows
  the type (a list for bool and choice, a number-only field for int and
  float); a value the type does not take is refused, and an empty one means
  "not set". `hidden: true` hides a row outside expert mode.
- A value edited on a plugin's page reaches that plugin's nodes in the
  Pipeline graph.
- **Plugins by purpose.** The Plugins tree and the Pipeline palette list
  plugins in collapsible branches (Conversion, Preparation, Detection,
  Sorting, …). The branch comes from the description's `category`, else the
  built-in table, else Other; the palette no longer leaves out a category
  missing from a fixed list. Enter on a palette entry adds it to the graph.

See `doc/ndmanager/README.md` — *Editing a plugin's parameters* and *Plugins
by purpose*.

</details>

## 2026-10-16 — Units: brain region, division, atlas and region ID

<details>
<summary>Details</summary>

Units carry four more optional fields after structure, type, isolation
distance, quality and notes: **region** (e.g. hippocampal formation),
**division** (e.g. archicortex), **atlas** (where the terms come from, e.g.
WHS SD rat v4 or Allen CCF) and **regionId** (a stable atlas or UBERON id).
Klusters edits them in Cluster Information and shows them in the palette;
the `.cli` cluster information file is format v2 (eleven fields; v1 files
read unchanged); ndManager's Units page and Record Curated Units carry them;
the session YAML's `units` entries write each key only when it is set. See
`doc/ndmanager-plugins/formats/cli.md`.

</details>

## 2026-10-16 — Klusters: saves are all-or-nothing and skip unchanged files; autosave covers hierarchical sessions; editing locked during a save

<details>
<summary>Details</summary>

- **All or nothing.** A save writes every file it changes (`.clu`, `.clc`,
  `.clp`, realigned `.spk`/`.res`/`.fet`, synced feature spaces, the units in
  the `.yaml`) in full beside its target first, then installs them together
  under a journal. A failure puts back what was already replaced; a crash in
  the middle is completed on the next open.
- **Unchanged files are not rewritten.** The ids are compared with the files;
  each working copy carries a stamp from when it last matched its file, so an
  untouched `.spk` is recognised without being read. A save with nothing to
  change writes nothing, and the readers are rebuilt only when waveforms were
  committed.
- **Autosave for hierarchical sessions.** The rescue copy now holds the atom
  ids too (`.<clc>.autosave`), from the same snapshot as the unit ids, and is
  restored with them when it fits. Autosave runs on the GUI thread from a
  timer (the old thread read the live clustering), only while there are
  unsaved changes, and follows a Save As.
- **Cluster information is saved with the clustering, not into the session
  YAML.** Klusters writes it to `<base>.cli.<method>.<group>[.<tag>]` beside
  the `.clu` (one per stage, in the same commit) and reads it back on open. The
  YAML `units` table is the record of finished curation: ndManager fills it on
  request with **Actions ▸ Record Curated Units…**, choosing per group which
  clustering's file to record.
- **No editing during a save.** Input is held while a save runs, with a
  "Saving…" box for a slow one. Closing during a save waits for it ("Waiting
  for the save to finish before closing…"), then asks to save only if the
  clustering still differs from the files.

</details>

## 2026-10-16 — Klusters: save-system fixes; Promote to Hierarchical Session

<details>
<summary>Details</summary>

An audit of the Klusters save path (Save, Save As, the pending-file commit,
the hierarchy siblings, autosave).

**Data that was lost and is now kept.**
- Save As wrote the clustering as last *saved* under the new name: the
  pending `.clu` it committed over the new file had not been updated. Every
  edit since the last save was missing from the new file, and the save
  reported success.
- Save As of a session with cluster information failed as "not writable"
  (the parameter file was looked for inside the new `.clu`'s path).
- An edit made while a save was running was marked saved, so closing
  afterwards asked nothing.
- The pending-file commit removed each original before copying over it; an
  interruption between the two lost the original, and the next open then
  deleted the pending copy too. It now copies beside the original and renames.

**Hierarchical sessions.**
- The `.clc`/`.clp` are taken from the same snapshot as the `.clu`, on the GUI
  thread (the save used to read the hierarchy maps on the save thread while
  edits changed them), staged, and installed only after the `.clu` is
  committed; a failed save leaves all three as they were.
- A sibling that could not be written failed silently; it now fails the save.
  A `.clp` kept because atoms straddle units is reported after the save.
- Save As writes the new triple and continues on it (it used to write the
  `.clu` alone, then later Saves wrote the new map into the old `.clp`).

**New: Hierarchy ▸ Promote to Hierarchical Session.** Writes a `.clc` (one atom
per unit, same ids) and `.clp` for a flat `.clu` — or adopts an orphan `.clc`
that nests in it — saves first if needed, and reopens the session
hierarchical. See `doc/klusters/hierarchical-clustering.md`.

Also: the hierarchy menu's editing entries were enabled in flat sessions after
a realign; save errors name the failing file.

</details>

## 2026-10-16 — ndManager: audit fixes — saves, Pipeline tab, Probes tab, Probe Maker

<details>
<summary>Details</summary>

An audit of ndManager, every finding checked a second time before it was
fixed (`claude/ndmanager-audit-2026-10-16.md` in the project notes).

**Crashes fixed.** Saving a session with two or more units (the units table
was filled with sorting on, scrambling rows and leaving empty cells), saving a
file channel mapping whose ids start at 0 or have gaps, opening a probe file
with a per-shank channel map or null fields, and malformed plugin
descriptions.

**Data that was lost and is now kept.**
- Skip flags: the page and the file used different keys, so every save
  wrote `skip: 0`.
- Failed saves were silent; "Save" in the close prompt closed even when
  nothing was saved; Save as Default marked the session saved; Reload
  discarded edits without asking and could turn a template into the file
  Ctrl+S overwrites; New from Project Template and Save could replace an
  existing file without asking.
- The session's `programs:` list was written alphabetically, so `ndm_start`
  was no longer first and ran steps in the wrong order. It keeps its order now.
- Pipeline tab: a node added after loading a pipeline file could reuse an
  existing id and replace another plugin; Apply replaced the Plugins tab
  (tuned values, help, programs outside the graph); graph edits were not
  counted as unsaved (the close prompt now offers to save the pipeline).
- Probe Maker: only the first edit after loading a probe was written.
- Probe changes rebuilt every group with default nSamples/peak/features and
  dropped `sdiffPairs`; unchanged groups now keep their settings, and groups
  get `probeId`/`shankIndex`.
- Plugin scripts lost their `#!` line; a session save cleared the "script
  modified" flag.

**Behaviour changes to know about.**
- ndManager reads and writes YAML only; convert XML sessions with
  `ndm_xml2yaml`. Saves always end in `.yaml`/`.yml`.
- Session-local probe files live next to the session file
  (`<session>.probe.<id>.probe`), not in the directory ndManager was started
  from; a session must be saved before probes are added. A removed probe's
  copy is deleted only when the session is saved.
- The probe library is searched in the session directory, `SESSION/probes`,
  `probeLibraryPath`, `$NEUROSUITE_PROBE_PATH`, then every XDG data
  directory; a typed channel offset is kept.
- `ndm_setupgroups` reads `channelOffset` as absolute (it added it to a
  running offset, so a second probe landed on the wrong channels) and finds
  ndManager's session-local probe files.
- The Probe Maker keeps each site's channel, writes sites top first, and a
  shank-length change keeps sites fixed relative to the tip.
- Pipeline tab: Clear keeps the root, cycles are refused, Apply orders the
  Plugins tab without discarding it, a pipeline file that fails to load is
  reported and protected, and Save Pipeline needs a saved session.
- Actions ▸ Query works again, on YAML files (paths like
  `acquisitionSystem/samplingRate`).
- `ndmanager --new` and `--model FILE`; Save as Default writes where File ▸ New
  reads; session YAML files are registered as `application/nphys-yaml`.
- `NDM_PIPELINE` naming a pipeline file that does not exist is now an error
  in `ndm_start` (it used to run another pipeline silently). Save Pipeline As
  under another name leaves the graph "unsaved" for the close prompt.
- A one-shank `.probe` without `count_per_shank` holds every geometry site in
  every reader (it did only in the Probe Maker).
- The installed MIME database and icon cache are refreshed once after the
  whole install; the unused postinst/postrm scripts are gone.

</details>

## 2026-10-15 — ndManager: New from Project Template; saves keep comments, inline lists and quoting

<details>
<summary>Details</summary>

**File ▸ New from Project Template…** (Ctrl+Shift+N) creates a session
parameter file from `templates/template.yaml` — every plugin with every
parameter it reads, and the comments documenting them. ndmanager asks where to
save the file, fills in today's date and opens it. The template is installed as
`<datadir>/ndmanager/templates/session-template.yaml`;
`NEUROSUITE_SESSION_TEMPLATE` points at another one. The template's probe
entries now use `probeId`, the key every tool writes.

**Saves keep the file's layout.** ndmanager, NeuroScope and Klusters wrote
parameter files back in yaml-cpp's block style: comments (including the
template's commented-out parameters) were lost, `channels: [0, 1, 2, 3]` became
one line per channel, and `'false'` lost its quotes and became a boolean for
Python readers. They now keep comments with the key or list item below them,
end-of-line comments, inline lists, quoting and spacing; saving an unchanged
file rewrites the same text. New lists of plain values are written inline.

</details>

## 2026-10-15 — Audit fixes: ndmanager keeps unknown YAML keys; aligners and re-extraction follow the .spk convention; re-extraction threshold base

<details>
<summary>Details</summary>

An audit of the shared libraries and the spike tools, each finding checked a
second time against code, docs and history before it was fixed.

**Behaviour changes to know about**

- **`ndm_reextractspikes` threshold scale.** The stderiv re-extractor now
  measures noise the way detection does (`thresholdNoise`, default `sdiff`)
  instead of always on the stderiv signal. On a session that does not set
  `thresholdNoise`, a tuned `reextractThresholdFactor` therefore gives
  thresholds higher by each channel's median|sdiff| / median|stderiv| ratio
  (about 1.6 on the reference session). Divide the factor by that ratio, or
  set `thresholdNoise: stderiv`, to keep the old thresholds.
- **ndmanager saves keep what they do not model** — per-group `probeId`,
  `shankIndex`, `sitePositions_um`, `klustakwik:`, `nCells`, extra
  acquisition fields and blocks such as `sourceRecording`. They used to be
  dropped on every save. Probe entries are now written with `probeId`; every
  reader accepts `probeId` and `id`.
- **Aligned `.spk` files made before this change mix two conventions**
  (shifted rows were re-cut with a zero seed and truncation). Re-extract them
  at their `.res`. Both aligners and the re-extractor now detect the `.spk`'s
  convention and follow it.
- Aligners never move a window to sample 0 or past the end of the `.fil`; a
  bare `stderiv` token on a session with `sdiffPairs` now aligns with the
  pattern, and `ndm_alignspikes_pca` accepts `stderiv_C4`/`_C5`.
- The sdiff detector and the re-extractors refuse (skip, for re-extraction)
  one-channel groups; re-extraction rejects a spike exactly one refractory
  period after the previous one, like detection.
- Groups with no channels keep their number (YAML readers, `ndm_xml2yaml`)
  instead of renumbering the groups after them.
- Zero-energy spikes get shift 0 in every xcorr back end and in centroid
  alignment; GPU ties pick the lowest lag like the CPU.
- `.wti`/`.wtl` window times and `.evt` times are written at full precision;
  binary `.res` detection no longer mistakes ~4% of files for text; a
  truncated `.col` or malformed `.tcl` is refused instead of half-read.

**Crashes and memory errors fixed:** detection on groups wider than 64
channels (stack overrun), a read past the previous buffer in all three
detectors, an untransformed look-ahead row at every 50,000-sample chunk edge
(could move a timestamp), uncaught exceptions on corrupt `.fet`/`.wtl`/`.eap`
headers.

</details>

## 2026-10-15 — Klusters: t-SNE of the oblique coordinates

<details>
<summary>Details</summary>

The oblique view (Shift+O) decomposes every spike on the template axes, one
coordinate per template, but can only draw two directions of them. With a basis
of **three or more templates**, **F** in the oblique view now runs t-SNE on the
spikes' full oblique coordinates instead of the features, so groups the 2-D
picture overlaps can pull apart. The embedding replaces the oblique view and is
an ordinary t-SNE: ↑/↓ steps the perplexity on the same coordinates, the lasso,
multi-lasso, watershed, zoom/pan and heatmap all work, Shift+O returns to the
oblique view and F to the features. With two templates the oblique view already
shows both coordinates, so F leaves it, as before, and says why.

</details>

## 2026-10-14 — ndManager: empty program parameters are no longer written as `~`

<details>
<summary>Details</summary>

Saving from ndManager wrote a parameter left blank as `value: ~` (YAML null).
Every reader in the suite treats that as unset, but only if it is new enough
to know the convention, and an older installed `ndm_*` script read the text
`~` as a value (`excludeClusters=~`). An empty parameter is now saved with its
name and status only and no `value` key, which every reader sees as unset.
Files saved earlier are cleaned up the next time ndManager saves them.

</details>

## 2026-10-14 — ndm_detectspikes (stderiv): refuse one-channel groups and flat signals

<details>
<summary>Details</summary>

A spatial derivative needs neighbouring channels. On a one-channel group (a
Plexon single-electrode session, say) the order-3 derivative is identically
zero, so the threshold came out 0, every sample was "detected" (9.1 million on
one group) and the extraction then rejected them all as flat. The detector now
stops before any work and names the group, suggesting derivative order 0
(`--method stderiv --methodOrder 0`), the standard method, or regrouping. It
also stops when a channel's detection signal is flat over the threshold window,
which gave the same zero threshold.

</details>

## 2026-10-14 — Klusters: crash on opening a session whose probe has no .probe file; leftover .pending copies

<details>
<summary>Details</summary>

**Crash fixed.** A `probes:` entry in the session YAML with no `probeFile`
(as a Plexon import leaves it) made the drift view look for the probe
geometry at the session directory itself; reading a directory throws a stream
error that the YAML parser does not translate, and Klusters aborted as soon as
the drift view was shown. A probe without a readable `.probe` file now simply
has no geometry, and any read error in the geometry lookup is caught.

**Leftover working copies.** Opening a session from its `.fet` before any
`.clu` exists left `.spk.pending`, `.res.pending` and `.fet.pending` copies
beside the originals, because the `.clu` copy failed after they were made.
They are now removed when the set cannot be completed.

</details>

## 2026-10-14 — Klusters: watershed split on the noise cluster

<details>
<summary>Details</summary>

**Shift+W now works on the noise cluster (1).** Select noise on its own and the
preview shows its density basins as usual; Enter carves every basin except the
largest out as a new cluster, while the largest basin (the noise core) and the
spikes no basin claimed stay in noise, so the noise bin always survives. The
commit applies the basins the preview drew, not a recomputation. Each new
cluster is its own undo step. Noise selected together with other clusters is
refused, because the regular split dissolves its sources and noise must not be
dissolved. The artefact cluster (0) is still never split.

</details>

## 2026-10-14 — Klusters: multi-lasso tool; zoom and pan in the t-SNE / oblique view

<details>
<summary>Details</summary>

**Multi-lasso** (Tools ▸ Multi-Lasso, toolbar, Ctrl+5). Closing a lasso keeps it
instead of cutting; draw as many as needed, then **Split by Lassos** (toolbar, or
Enter with no lasso open). Every distinct combination of lassos a spike falls in
becomes one new cluster — two overlapping lassos give *A only*, *B only* and *A
and B* — and spikes in no lasso stay where they are. Backspace removes the last
lasso, Esc discards them all. A lasso is resolved to its spikes when it closes, so
lassos can be drawn across zooms, projections and the scatter/embedding switch and
still intersect. Each region is its own undo step; the selection lands on all the
new clusters.

**t-SNE / oblique navigation.** The embedding now zooms (Ctrl+wheel, toward the
cursor), pans (Ctrl+drag) and resets (double-click) like the feature scatter; the
lasso, the density heatmap and the watershed preview follow the zoom. The
embedding's title, previously black on the black background, is now readable.

</details>

## 2026-10-12 — Klusters: Shift+H density heatmap over the feature view

<details>
<summary>Details</summary>

**Shift+H** toggles a point-density heatmap over the cluster view, in the
feature scatter, the oblique projection and the t-SNE embedding alike. It
shows the density of the points the view draws (the shown clusters, with the
same out-of-scope filter), binned at screen resolution so it follows zoom and
pan, log-scaled and drawn semi-transparently: dense cores read warm, sparse
regions stay clear so their points remain visible. The key is rebindable
(`cluster.densityOverlay`). The smoothing kernel is a Cluster View preference
(**Density heatmap ▸ Smoothing kernel σ**, 0–30 px, default 3 px; 0 shows the
raw binned counts).

</details>

## 2026-10-12 — NeuroScope: closing a session no longer deletes or overwrites parameter-file values

<details>
<summary>Details</summary>

NeuroScope saves the session's parameter YAML on every close, modified or not,
through `ParameterYamlModifier`. Several of its setters rebuilt their section
from only the keys NeuroScope models, so opening and closing a session
silently deleted keys written by other tools:

- **Spike groups:** `sdiffPairs` (the stderiv custom-difference channel
  algebra), `probeId`, `shankIndex` and any other per-group key were dropped.
  An earlier fix kept only `nSamples`/`peakSampleIndex`/`nFeatures` by name.
  Group nodes are now carried across whole, paired by channel set and then by
  position.
- **`files:`:** ndmanager's per-file `channelMapping` was dropped. Entries are
  now updated in place, matched by extension.
- **Anatomical groups, channel colours and offsets:** extra keys were dropped,
  and a channel NeuroScope had no value for got the default instead of the
  file's value. The display trash (group 0) was also saved as an ordinary
  group, so it read back as group 1 and shifted every real group. The trash is
  no longer saved; the reader rebuilds it from the unlisted channels, as the
  XML reader did.
- **Acquisition:** `offset: 0`, and a non-integer `amplification` or
  `voltageRange` (e.g. `192.5`, `1000.0`), read as absent. NeuroScope then
  substituted its preference default and saved it over the file. A value that
  already denotes the same number is now left as written.
- **No-op saves:** a save that changes nothing no longer rewrites the file, so
  comments, formatting and mtime survive a plain open/close.

`klustersshared_yamlmodifier_test` (`NS_BUILD_TESTS`) round-trips a session
holding these keys through the same setters NeuroScope's `saveSession()` uses.

</details>

## 2026-10-12 — ndm_plx2dat: Plexon .plx / .pl2 → .dat + .evt with header metadata in the session YAML

<details>
<summary>Details</summary>

New acquisition converter for Plexon recordings, both containers (detected
from the file signature). **`process_plxconvert`** (C++20, no dependencies)
exports one continuous stream — `WB` by default; `stream` / `channels` pick
another or a subset — as raw int16 counts into `SESSION.dat`, and writes the
Plexon header metadata to `SESSION.plexon.yaml`; **`process_plxyaml.py`**
merges it into the session YAML (`acquisitionSystem` from the Plexon gain,
`generalInfo` date/description when empty, and a `sourceRecording` block:
headers, per-channel names / sources / µV per count, the `.dat` time origin
and pauses, spike- and event-channel summaries, OmniPlex filter settings,
findings). The timeline is rebuilt from block timestamps, so a paused
recording keeps its time base (gaps zero-filled and listed) and the `.dat`
stays aligned with Plexon's spike and event timestamps; channels without
data are kept as zero channels so channel numbering survives.
Readable text of the PL2 headers and settings region that the decoded fields
do not hold (device chain, electrode and input types, source configuration)
is kept as `headerText` / `deviceText`.

Every digital event (single-bit inputs, strobed words, keyboard, start/stop)
goes to a NeuroScope event file `SESSION.plx.evt` (id: `evtId`), time-sorted,
in ms from `.dat` sample 0, labelled by channel name (plus the value when a
channel's values vary, always for strobed words), with `gap start` / `gap
end` markers around zero-filled pauses. The command page lists what none of
the three outputs carries (other streams' samples, Plexon's online spikes and
sort definitions, the PL2 footer, unknown record types).

The other continuous streams with data (`extraStreams`, default all but
`SPKC`) go to `SESSION.px<stream>` files (`.pxai`, `.pxfp`, …): the `.dat`
layout at each stream's own rate, aligned to the `.dat` start on the stream's
own grid; files with the `.dat`'s channel count are listed under `files:` for
NeuroScope. Format: `doc/ndmanager-plugins/formats/pxstream.md`.

Plexon's online spikes (`exportSpikes`, default on) go to
`SESSION.pxres.N` / `.pxclu.N` / `.pxspk.N` per spike channel: `.dat` sample
times, cluster id = Plexon unit + 1, raw waveforms — the byte formats of text
`.res` / `.clu` and `.spk` under their own types, because `.res` is Shared
across method tokens and a Plexon copy would shadow the pipeline's. Format:
`doc/ndmanager-plugins/formats/pxspikes.md`.

PLX follows Plexon.h as read by python-neo; PL2 follows OpenReadout's
clean-room notes (Plexon publishes no PL2 specification), so every run
checks internal consistency and reports failures under
`sourceRecording.findings`. Throughput on a 2-core VM: 0.8 s for a 330 MB
PLX of 1.5 M blocks, 0.6 s for a 300 MB PL2. Test:
`src/ndmanager-plugins/test/test_plx2dat.py` (synthetic files from an
independent encoder; set `PROCESS_PLXCONVERT_BIN`). Reference:
`doc/ndmanager-plugins/commands/ndm_plx2dat.md`.

</details>

## 2026-10-12 — Klusters: sync other feature spaces to the current spike times

<details>
<summary>Details</summary>

**Sync.** Realign and nudge rewrite only the `.clu` space's `.spk`/`.fet`, so
the group's other method spaces drift from the shared `.res`. **Actions → Sync
Feature Spaces** re-extracts each waveform file the `.clu` space does not use
from `.fil` at the session's spike times (same window cutting and stderiv
transform as the pipeline; a `.spk` shared by several spaces — a `_D` space
reads its waveform token's — is rebuilt once) and re-projects each space's
`.fet` through its own `.pca` basis with the kernel realign uses — a `_D`
lag basis is an ordinary PCAE basis on a widened window, so lag spaces are
covered; extra non-PCA columns are carried over. A preference (Refinement →
*Sync a feature space … when switching to it*, off by default) does it for the
target space on every switch. Results are `.pending` working copies: the Space
box loads them, Save commits them, closing without saving, Save As or undoing a
realignment discards them. Reference: `doc/klusters/README.md` → Feature spaces.

**Neighbour clamp removed.** The 2026-10-11 clamp that held a realigned or
nudged spike short of its neighbour in `.res` is gone: what the files share is
the row index, which realign and nudge keep (each spike is rewritten in its own
row; the reorder step stays removed), and every `.spk`/`.fet` follows the one
`.res`, so the order of times across clusters does not matter. Every spike now
moves the full shift. `spikeorder.h`, its test and the held-back count are
removed.

**Sort Spikes by Time.** A new action, **Actions → Sort Spikes by Time**, puts a
group's rows back in time order. It sorts the saved `.res` by time (stable)
and applies the same row permutation to every `.spk` file, each method's
`.fet` and every `.clu`/`.clc`; a file shared by several spaces is permuted
once. It needs a saved document, swaps all files in together
(rolling back on any failure), and reopens the document. `.eap` membership is
reported, not reordered, and any other file naming the group is listed.

**Every space in memory.** Klusters now holds every usable feature space's
`.fet` table and `.spk` waveforms in memory, loaded at open and refreshed after
each switch, sync and Save (a file is re-read only when it changed). Switching
reads nothing from disk, Sync Feature Spaces works from the held tables, and
waveform reads come from memory (`SpkMemoryCache`; a file too large to hold
falls back to reading from disk). Realign and nudge edits are still read
through the overlay; at Save the readers are reloaded before it is cleared.

**Realign and nudge from any feature space.** A Refinement preference (on by
default) lets realign, Align All, nudge and the post-edit realign run while
another space is shown: Klusters swaps to the `.clu`'s own space, runs them,
and swaps back. Afterwards every other space follows the moved spikes only —
each is re-extracted from `.fil` into every other waveform file in that
space's domain (shared cutting with the full re-extract, `spikeCutSpecFor`)
and re-projected into every space's held table and `.pending` `.fet`. Each
space records the spike-times generation it is current to, so one that was
already behind gets a full sync instead of a partial patch. Switching is no
longer refused while a realignment is unsaved.

**Follow-ups.**
- Realign and nudge apply the session's `sdiffPairs` pattern only when the
  method token allows it (`_C<n>` or bare `stderiv`; not `_S<n>`), the same
  rule re-extraction uses.
- A synced space whose spike times changed since its sync (a later realign or
  nudge) is marked in the Space box tooltip as needing another sync.
- Save As drops synced copies on the GUI thread, before the save thread runs.
- Realign warns when a cluster's own spikes leave time order (correlograms and
  ISI assume they do not); Sort Spikes by Time repairs it.

**Fix.** A session working copy such as `x.spk.stderiv_C5.7.pending` was read
as the *standard* method (`custody::methodOf` needs the group as the last
token), so re-extract-on-save rebuilt a stderiv `.spk` as raw waveforms.

**Not yet verified in a build** (no Qt6 here): needs a workstation build and a
pass on the reference session — sync after a realign, switch, check the scatter
and waveforms line up; Save, reopen, switch again.

</details>

## 2026-10-11 — Klusters: view one .clu through another method's feature space; spikes never change order

<details>
<summary>Details</summary>

**Feature-space switcher.** A **Space** box at the far right of the Klusters
Parameters toolbar shows the method space whose features and waveforms are
loaded and lists every space the open group has on disk (`standard`,
`stderiv_C5`, a `_D` lag space, …). Picking another loads that space's `.fet`
and `.spk` under the clusters being curated — cluster assignment, unsaved edits
and undo history are kept — so one curation can be compared across feature
spaces. Save always writes the `.clu` under its original method. While another
space is shown, the operations that write the `.clu` space's own files are off
(realign, Align All, nudge, Save As, decollide, template generation; the
post-edit auto-realign is skipped); recluster works on the shown features.
Spaces whose `.fet`/`.spk` do not match the open spike count are listed greyed
out with the reason. Reference: `doc/klusters/README.md` → Feature spaces.

**Spikes never change order.** The switcher relies on every file of a group
being indexed by the same spike row, so realign and nudge no longer reorder
spikes. Each spike's shift is clamped between its neighbours in `.res`
(`spikeorder.h`: a neighbour from another cluster is a hard wall; a neighbour
that also moves splits the gap). Realign's old reorder step — it permuted a
cluster's rows by new timestamp, and could leave `.res` out of order against
other clusters' spikes — is removed; the log and the nudge status bar report how
many spikes were held back. New unit test `klusters_test_spikeorder`
(fixed cases plus a randomized never-reorders property check).

**Not yet verified in a build.** No Qt6 toolchain was available, so the Klusters
changes were checked by review only (the `spikeorder` test was built and passes).
Needs a workstation build and a pass on the reference session: switch spaces
and back with unsaved edits, Save while another space is shown, realign/nudge
held-back counts, and a recluster from a `_D` space (its feature mask uses the
YAML's features-per-channel).

</details>

## 2026-10-10 — Input-binding overhaul completed across Klusters and NeuroScope; Qt 6.4–6.12 compatibility; documentation

<details>
<summary>Details</summary>

**Input system — one engine, both GUIs.** The keyboard/mouse binding engine (the
`input::` registry: device-agnostic chords, self-describing scopes in a tiered
stack, Passive/Exclusive capture, and the gesture "seam" that keeps a drag/commit
body in the view while the registry owns the trigger) now lives in
`libklustersshared` and drives **both** Klusters and NeuroScope through one
process-wide `input::registry()`. In NeuroScope every menu/tool shortcut, the
TraceView tool gestures (zoom, select-channels, measure, select-time, select-event,
add-event, draw-line) and the Position/Spectral frame zoom resolve through the
registry; the old ~470-line `TraceView::mousePressEvent` switch is gone. Users get
a generated **Preferences ▸ Input** page (rebind any key or mouse binding, clash
highlighting, reset), saveable **keymap layouts** with a bundled read-only
*Default*, a registry-generated **Help ▸ Keyboard Shortcuts** cheat-sheet, and a
per-display active-tool indicator. References: `src/klusters/docs/INPUT_SYSTEM.md`
(the engine) and `src/neuroscope/docs/INPUT_SYSTEM.md` (NeuroScope's adoption).

**Qt 6.4–6.12 compatibility.** A static audit plus a real 6.4.2 build and a 6.10
check: the code uses only low-churn Qt modules and no API removed within the Qt 6
series, so it is compatible across the span. Fixed the one real break (reduced
transitive includes on Qt 6.9/6.10 — include what we use), a Qt 6.8+ teardown
warning (wildcard `disconnect` of `destroyed()`), retired `qAsConst` for
`std::as_const` (deprecated in 6.6), declared the **Qt 6.4 floor** in the
`find_package` calls, and bumped the stale `cmake_minimum_required(2.8.6)` in
`ndmanager-plugins/matlab`. Remaining: a CI build matrix for 6.4 / 6.10 / 6.12.

**Documentation.** Developer references for the input system (above) and
user-handbook sections (Preferences ▸ Input, keymap layouts, the shortcuts
cheat-sheet, the active-tool indicator) in `src/neuroscope/doc/en/index.docbook`.
Plus a prototype modern documentation system (`doc/site/`) — MkDocs + Material +
pandoc — that converts the DocBook handbook with a toolchain that installs
anywhere (replacing the retired KDE meinproc path), unifies it with the developer
docs, and publishes to GitHub Pages; see `doc/site/README.md`.

</details>

## 2026-10-09 — NeuroScope: two silently dead shortcuts fixed

<details>
<summary>Details</summary>

Mirroring NeuroScope's menu shortcuts into the shared input registry (above) let a
startup conflict audit see them for the first time, and it found two keys bound
twice. Qt treats a shortcut claimed by two main-window actions as ambiguous, so
**neither action fired reliably**:

- **Ctrl+Shift+A** — *Select All except 0 and 1* and *Autocenter Channels*.
- **Ctrl+Shift+S** — *Save As* (`QKeySequence::SaveAs`) and *Skip Channels*. Only
  on Linux/Windows, where the standard Save As sequence is Ctrl+Shift+S.

Each chord now stays on its first-listed action (**Select All except 0 and 1**,
**Save As**). **Autocenter Channels** and **Skip Channels** keep their menu entries
but no longer have a default shortcut; bind one in Preferences ▸ Input if wanted.
The startup audit now reports no conflicts. The Ctrl+Shift+S collision was
confirmed live on Qt 6.10 (it is invisible in a headless build, where
`QKeySequence::SaveAs` resolves empty).

</details>

## 2026-10-08 — ndm_hipass: zero-phase Butterworth filter (new default)

<details>
<summary>Details</summary>

**New `process_butterworth`** (CPU/OpenMP, CUDA when built with it and a device
is present): Butterworth high-pass and optional low-pass, designed in double
precision as second-order sections (bilinear transform, pre-warped corners,
per-section unit passband gain), run forward then backward (zero phase, −6 dB at
each corner, effective order 2 × `order`), odd-reflected at the file edges.  The
work is split into independent channel × block units with a settling margin taken
from the filter's impulse response (start-up error < 1e-3 LSB for any int16
input; 240 samples at 800 Hz, order 3, 32.5 kHz), so blocks run in parallel and
the result does not depend on the chunk or block size.  Precision `auto` = double
on the CPU, single on a GPU; `-p` overrides.

**`ndm_hipass` gains `filter: butterworth|median`, default `butterworth`**, with
`highPassHz` (800), `lowPassHz` (0 = off), `order` (3), `precision` (auto).
`filter: median` keeps the old median-subtraction filter; `windowHalfLength` is
now needed only for it.  Behaviour change: a session whose `ndm_hipass` node
does not set `filter` now gets the Butterworth `.fil`.

Why: the median high-pass is non-linear — `x[t] − median(window)` is exactly 0
whenever the centre sample is the window median (about 5% of samples, runs up to
~17 on spike downstrokes), its effective −3 dB corner at `windowHalfLength: 16`,
32.5 kHz is ~800 Hz (not ~600 as documented), and it adds broadband content above
10 kHz that the `.dat` does not contain (coherence 0.01 there).

Verified on the reference `.dat` chunk (group 6, 36–41 min, 8 channels, 300 s):
double output equals scipy `sosfiltfilt` rounded to int16 on every sample (high-
pass 800 Hz; band-pass 600–6000 Hz); identical across chunk/block sizes; single
differs from double on 0.04% of samples by 1 LSB (1 of 59,675 threshold
detections moved by 1 sample); single and double take the same time on the CPU
(0.9 s for the chunk on 2 threads).  The CUDA source compiles for sm_86/sm_89
(clang, device code through ptxas) and the CUDA-enabled binary falls back to the
CPU without a device; a host emulation of the kernel's unit mapping and scratch
layout is bit-identical to the CPU single path.  **The kernel has not been run on
a GPU**, and the Qt6/CMake build was not run in this environment.

</details>

## 2026-10-08 — ndm_extractspikes filter: check the raw source window

<details>
<summary>Details</summary>

The post-extraction flat-run test looked only at the extracted `.spk`, so with a
`stderiv`/`sdiff` method it missed dropped-out channels: the spatial derivative
mixes a dead channel with its live neighbours and the result is not flat.
`process_spikefilter.py` now also tests the raw window of the extraction source
(`<session>.<inputExtension>`, the standard-extraction window
`[t-(peak-1), t-(peak-1)+nSamples)`) on the group's channels, and drops a spike
that fails either test.  With one channel zeroed for 1 s and one stuck for 0.5 s,
a `stderiv_C5` extraction caught 0 of the 80 affected spikes in the `.spk` and
all 80 in the raw window.  Short exact-zero runs produced by the median high-pass
(`ndm_hipass` output is exactly 0 where the centre sample is the window median) mean `flatRunMax` ≈ 20 is the useful setting (1 other spike dropped,
against ~31% at 5).

</details>

## 2026-10-08 — ndm_pca: a `_D` run adds the lag space to existing features

<details>
<summary>Details</summary>

`ndm_pca --method stderiv_C5_D34` skipped every group whose ordinary
`.fet.stderiv_C5.N` already existed, so the lag space could not be added to a
session that had its ordinary features.  A `_D` run now writes only the missing
outputs: with the ordinary `.fet` present it refits, keeps the stored
`.fet`/`.pca` (the refit copy goes to a scratch directory), and writes
`.fet`/`.pca.<token>_D<lag><dims>.N`; it warns when the refit basis differs from
the kept `.pca`.  With the lag `.fet` present it writes the ordinary pair only;
with both present the group is skipped as before.  Runs without a `_D` suffix
are unchanged.

</details>

## 2026-10-08 — ndm_extractspikes: post-extraction flat-run / duplicate filter

<details>
<summary>Details</summary>

**`flatRunMax`, `dedupSamples` in the `ndm_extractspikes` node** (new
`process_spikefilter.py`, any method).  Each freshly extracted `.spk` is checked
in its own transform domain: more than N identical consecutive samples on any
channel, or a position within N samples of the previous kept spike (default 0 =
identical positions; -1 off).  The `.res` is shared by every waveform variant, so
the keep-mask is applied to the shared `.res` and every per-spike file of the
group still at the original count (`.spk`/`.fet`/`.clu`/`.clc` of any method,
post-group stages); other counts are reported and left; symlinked aliases are
filtered once; `<res>.prefilter` keeps the original times.  Verified end to end:
all filtered files equal the original rows under the mask and both `.spk`
variants are byte-identical to fresh re-extractions at the new `.res`.

</details>

## 2026-10-08 — ndm_detectspikes (stderiv): flat-run rejection and duplicate removal

<details>
<summary>Details</summary>

**`flatRunMax`** (engine `-F`, default off) rejects a spike whose raw window has
more than N identical consecutive samples on any channel (zero-filled gaps,
stuck lines).  **`dedupSamples`** (engine `-U`, wrapper default 0) drops a spike
re-centred onto (or within N samples of) the previous kept one — the historical
re-centring window collapses a few pairs onto one sample.  Also fixed: when the
waveforms are streamed to disk (larger than `memCapGB`), a rejected spike was
skipped instead of written as a placeholder row, so the compaction that drops
rejected rows by index shifted every later waveform by one (8,484 of 8,485
misaligned under `-F` in a test; latent before, since the stderiv flat-channel
test practically never fires).  Note: a median-high-passed `.fil` has frequent short
per-channel zero runs (filter output, not dropouts); `flatRunMax: 5` removes 31% of spikes
there — see `doc/ndmanager-plugins/commands/spike-detection.md`.

</details>

## 2026-10-08 — custody: `.spk`/`.res` of a `_D` feature-space token resolve to its waveforms

<details>
<summary>Details</summary>

**Shared-artifact resolution.**  `_D<lag><dims>` names a feature space over the
waveforms of the token without it, but `resolve()` fell back
method → standard → untagged, so Klusters opened on `.clu.stderiv_C5_D34.N`
read the raw `.spk.standard.N`.  Shared types now try the waveform token
(`stderiv_C5_D34` → `stderiv_C5`) right after the exact method, and offer that
path when nothing exists, so a Klusters realign or Save As writes the one real
`.spk`.  This replaces the `.spk.<…>_D34` copy/link: a link is renamed to
`.stalebkp` by fiber-kit's de-dup and replaced by an independent copy on a Klusters
realign save (leaving the `stderiv_C5` waveforms out of step with the updated
`.res`).  Same rule in `custody.hpp`, `ndm_custody` and `ndm_resolve_io.py`;
eight new rows in `custody_vectors.tsv` (95 checks, all three runners pass).

</details>

## 2026-10-08 — ndm_pca: lagged feature space (`_D<lag><dims>`)

<details>
<summary>Details</summary>

**ndm_pca — `method: <token>_D<lag><dims>`.**  The lagged feature space
(per channel PC1 at −lag/0/+lag samples, + PC2 when dims is 4) existed only in
fiber-kit (`fiber-session --feat-lag … --emit-fet --emit-pca`).  `ndm_pca` now
produces it: a `_D` suffix on the method (e.g. `stderiv_C5_D34`) reads the `.spk` of
the base token, writes the ordinary `.fet/.pca` as before, and also writes
`.fet/.pca.<token>_D<lag><dims>` from the same fitted, varimax-rotated
eigenvectors (`process_pca --lag/--lag-pc2/--lag-fet`).  The lag basis is an
ordinary PCAE file on the window widened by the lag (centered 0, zero means), so
the lag `.fet` is the plain projection of the `.spk` through it.  Verified equal,
value for value, to fiber-kit's `lag_basis`/`lag_project` on the reference chunk;
without `--lag` the outputs are byte-identical.  See
`doc/ndmanager-plugins/commands/pca.md`.

</details>

## 2026-10-08 — stderiv detection: threshold noise base, re-centring, hybrid arm

<details>
<summary>Details</summary>

**ndm_detectspikes (stderiv) — `thresholdNoise`.**  The stderiv detector
measured its noise σ on the spatial derivative but thresholded the spatial +
temporal derivative, whose noise is ~0.6× smaller on the reference session, so
`thresholdFactor` 0.8 meant ~5.3σ of the detection signal instead of 3.2σ.
New optional `thresholdNoise: stderiv` (engine `-T stderiv`) measures σ on the
signal actually thresholded — the convention `ndm_reextractspikes_stderiv`
already used.  Default `sdiff` keeps the historical behaviour (default run
byte-identical `.res`/`.spk` on the reference chunk).  Every run now prints the
effective threshold in detection-signal σ.  See
`doc/ndmanager-plugins/commands/spike-detection.md`.

**ndm_detectspikes (stderiv) — `recenterMode`, `recenterHalfWidth`.**  Where
each detection is placed before the shared `.res` is written: `stderiv`
(default, historical: max Σ|stderiv|) or `raw` (the group-referenced raw
trough), over the historical window or ±`recenterHalfWidth` samples (engine
`-C`, `-W`).  The historical window moves ~10% of detections by >5 samples on
the reference chunk.  `raw` ±8 sharply tightens units whose stderiv waveform
has several near-equal extrema (unit 26 stderiv-space dispersion 15.2 → 1.7)
with the median unit unchanged.  Also fixed: the engine wrote the re-centred
times to `.res` only when a spike was rejected or the re-centring reordered
spikes; otherwise `.res` kept the pass-1 detection times while the waveforms
were cut at the re-centred positions.  The final timestamps are now always
written.  **Behaviour change for existing sessions:** whether a group's `.res`
was re-centred depended on whether re-centring happened to reorder that group's
spikes.  On the reference chunk as one 8-channel group it did (output unchanged
by this fix); split into two 4-channel groups it did not, and 305 and 1287
spikes had `.res` ≠ waveform position.  Existing `.res` files may therefore be
un-re-centred for some groups; re-run detection to make them consistent.

**ndm_detectspikes (stderiv) — `detectArms: hybrid`.**  Optional second
detection pass on the group-referenced raw signal (engine `-H`), merged with the
stderiv pass by the refractory rule; each arm thresholds at
`thresholdFactor × 4σ` of its own signal (implies `thresholdNoise: stderiv`).
Recovers spikes whose footprint spans adjacent sites, which the spatial
derivative attenuates; on the reference chunk at ~5.3σ per arm it raises
detections 11.5k → 28.3k, mostly multi-unit activity, with genuine missed spikes
for at least one high-rate unit.  Default unchanged (byte-identical).

</details>

## 2026-10-08 — Klusters: rebindable keyboard + mouse, and the Overview interaction redesign

<details>
<summary>Details</summary>

Work from 2026-10-06 to 10-08 that put every Klusters key, mouse button and wheel
binding on the `input::` registry (the engine the 2026-10-10 entry describes) and,
on top of it, redesigned how the Overview Display is navigated. Reference:
`src/klusters/docs/INPUT_SYSTEM.md`.

**What changes for the curator:**

- **Navigation is uniform.** Ctrl+wheel zooms and Ctrl+left-drag pans in the
  feature, trace and matrix views. On the waveforms and the auto-correlogram
  Ctrl+wheel scales **amplitude** instead; on the trace, Ctrl+Shift+wheel scales
  amplitude. The trace Ctrl-pan scrolls on through the recording at the window edge,
  and trace amplitude auto-fits the data on first load.
- **No default tool.** The rubber-band Zoom tool is retired; the data views open
  with a plain pointer.
- **The four curation tools are toggles.** New Cluster (Ctrl+1), Split (Ctrl+2),
  Delete Artifact (Ctrl+3, new) and Delete Noise (Ctrl+4, new) are
  mutually-exclusive checkable toolbar/menu buttons; selecting the active one again
  returns to the pointer. Each display keeps its own tool and the toolbar tracks the
  active display.
- **A plain click picks.** In the feature view it scrolls the trace to the nearest
  spike, in any projection; the Select Time tool is gone. In the trace, a click
  selects the nearest spike's cluster from **all** clusters, and Shift+click adds it
  to the selection.
- **Smaller changes.** Trace double-click is disabled (it crashed after a click had
  changed the selection). A double Escape clears the cluster-palette selection.
  Trace Prev/Next spike is Ctrl+Shift+Right/Left and steps through the whole
  recording when no cluster is selected.

**Preferences ▸ Input** is generated from the registry. Every command is listed,
including menu actions that ship without a shortcut, with a key, mouse-button or
wheel editor per row, conflict highlighting, per-row reset and "Reset all to
defaults". **Keymap layouts** save and reuse a whole binding set, with a bundled
read-only *Default*. **Help ▸ Keyboard Shortcuts** gains a registry-generated Mouse
section.

**Internals.**
- The TraceView press monolith is split into one command per tool mode on a shared
  click-geometry preamble (`resolveClickGeometry`, `nearestChannelAt`).
- The four curation matrices share their Ctrl navigation by composition
  (`MatrixNavigator`, `registerMatrixNav<V>()`, `matrixResolveClick()`), not a
  common base class.
- Four structural key handlers (the Esc family, Left/Right tab switching,
  Tab/Backtab, PageUp/Down) stay inline in `eventFilter` by design;
  INPUT_SYSTEM.md explains why.

**Live-pass status.** Confirmed on the workstation: the Preferences focus and
conflict-tint fixes, the waveform Ctrl+click channel pick, and the trace
Ctrl-zoom/pan. Everything else is behaviour-preserving by construction but still
wants a live pass. If a binding seems to have regressed, check Preferences ▸ Input
first: a stale saved override can mask a binding and look exactly like a code bug.

</details>

## 2026-10-06 — Klusters: paint-structure refactor

<details>
<summary>Details</summary>

A staged refactor of the classic views' 2003-era paint machinery (ClusterView,
WaveformView, CorrelationView, ErrorMatrixView, TraceView), so overlays and new
views are cheap to add. Reference: `src/klusters/docs/VIEW_SYSTEM.md` §2.2.

- **Monotonic `invalidate(level)`.** All 150 call sites that raised the shared
  `drawContentsMode` flag now go through `BaseFrame::invalidate`, which only ever
  raises it toward a full redraw. A later refresh can no longer cancel a queued
  redraw within the same paint; that was the cause of the lineage overlay sometimes
  not drawing on entry, now fixed. The one legitimate lowering, each `paintEvent`'s
  terminal reset, stays direct.
- **`BufferedView` base** (`BaseFrame → BufferedView → {ViewWidget, TraceView}`). It
  owns the double buffer and a template-method `paintEvent`. WaveformView,
  ClusterView and CorrelationView are migrated. ErrorMatrixView and TraceView keep
  their own `paintEvent` by design (device-coordinate matrix; init/resize prelude),
  with a comment saying why. Incidental fix: the buffer is now sized to the
  viewport; it used to be 10 px larger, which rebuilt it on every paint.
- **Shared geometry.** WaveformView's per-channel baseline math (six open-coded
  copies) goes through `channelStep()` / `channelBaselineY()`.

No behaviour change intended beyond the redraw fix. Optional follow-ups (a
coordinate-space RAII guard, an overlay registry, folding the newer matrices onto a
shared base) are listed in `doc/dev/ROADMAP.md`.

</details>

## 2026-10-05 — Klusters: template curation in the feature space

<details>
<summary>Details</summary>

The curator now builds each template class by hand in Klusters, instead of
`fiber-template` inventing the drift/adaptation binning. The class is a lineage
tree: one drift root per session time region, with lower-amplitude
(adaptation) and locked-pair collision leaves. Native generation becomes the final
refinement step. Covers work from 2026-10-05 to 10-06. File references:
`doc/ndmanager-plugins/formats/wtl.md`, `wti.md`, `wtf.md`.

**Model and files.**
- **A node is a running `{mean, std, count}` summary, not a spike list.** Folding a
  selection combines summaries exactly with the parallel-variance formula. The
  **mean** is used, not the median, because it recombines exactly from summaries.
  Guarded by `core_test_running_stats`.
- **Separate files.** `.wtl` (v2) is the editable lineage source. `.wti`/`.wtf`
  (v2: a `parent` column and a `collision` link kind) remain the auto-generated
  library. The committed model renders to its own **`.mti`/`.mtf`**, using the same
  on-disk schema as `.wti`/`.spk`. This fixes a commit of an all-placeholder lineage
  overwriting the real library; such commits are now refused.
- **Native generation.** `template_generate.hpp` in neurosuite-core is a
  byte-validated port of `fiber_template.py`'s `generate()` and can run per cluster;
  the external plugin is now a fallback.

**Editing (Shift+E, in the feature view).**
- **Preferences ▸ Display ▸ Templates mode** is the master switch (on by default).
- The overlay forces time × the best energy axis and draws each region's root as a
  ribbon above the scatter, with leaves below. A fresh session is auto-seeded with
  empty placeholders.
- Shift+left-drag moves a region boundary. Right-click anywhere in a region adds
  drift/adapt/collision nodes, splits or commits. Edits auto-save and have their own
  Undo/Redo in that menu.
- A left-column **Template palette** lists the classes: ★ marks the primary, which
  `s` or a click toggles; New and Delete; an absolute/best-fit scale checkbox
  (absolute by default).
- The primary class's **mean±std band** draws over the live waveforms, in review
  mode too, and tracks amplitude scaling and cluster changes.

**Matrices.** Marking nodes (click, rectangle lasso, or right-click) adds
cluster×template rows/columns, in time order, to all four curation matrices:
- Template, Drift and Residual show their own statistic.
- Error is all grey; it has no template equivalent.
- Cells where the cluster has no spikes in the node's window are dimmed, not hidden.
- Clicking a template cell selects that cluster and overlays only that node.

The template strip now honours all six scoring modes, and the raw metric is
normalised in log space so pairs are distinguishable. The matrices' shared code was
consolidated into `MatrixTemplateStrip` and `MatrixViewport`, with a geometry unit
test.

**Fixes found in live runs.**
- A Sort-Clusters event filter swallowed every right-click before the lineage menu
  could open.
- Folding a selection read a non-existent stage-tagged `.spk` path, so regions never
  filled.
- Deleting a class left its ribbon on the scatter.
- Palette `s` was bound to oblique-basis pinning and eaten by list type-ahead.
- A per-node waveform stagger was tried and reverted.

**Open.** Most GUI items are compile + link verified and want a live run. The
Template Library tab is still present as the advanced-CRUD fallback. fiber-kit's
`.wtl` reader still speaks v1 and needs a separate fiber-kit patch.

</details>

## 2026-06-08 — Klusters high-cluster-count rendering perf, PCA-Center Align All profiling + setup hoist, cross-cluster GPU-batch design

<details>
<summary>Details</summary>

A performance session focused on sessions with thousands of clusters: the
remaining per-cell matrix render hotspot, palette rebuild cost, and an
investigation into why "PCA-Center Align All" runs sequentially and slows as it
progresses.

**Klusters — template matrix rendered as one image.** `TemplateMatrixView::drawMatrix`
filled every cell with its own `fillRect` (≈9 M calls at 3000 clusters) on every
paint — the same hotspot already fixed in the error matrix.  It now builds an
8-bit indexed `QImage` (one pixel per cell, `NB_COLORS` color table, diagonal →
black) by scanline and blits it once with `drawImage` scaled to the matrix rect
(smoothing off).  The white at/above-threshold outlines are kept as a separate
cheap scan pass (per-cell score compare, `drawRect` only for cells above
threshold) and the yellow selected-pair outlines are unchanged; color maths is
identical.  Rendering change — worth a visual confirm on a real session.

**Klusters — palette color swatches cached.** The palette rebuild created a
fresh `QPixmap` + `QPainter` per cluster just to produce a solid color swatch
(filling with `backgroundColor` then overpainting the whole 12×12 with the
cluster color).  Cluster colors cycle a small fixed palette, so the swatch is
now cached in a `QHash<QRgb,QPixmap>` and reused — a rebuild creates only as many
pixmaps as there are distinct colors.  `QPixmap` is implicitly shared, so reuse
is free; visually identical.

**Klusters — PCA-Center Align All: opt-in per-phase timing.** Investigation
confirmed the batch is sequential (one `RealignWorker` per cluster, the next
launched only after the prior finishes) and that the accept path is cheap
(`forceClusterRefresh` early-returns for clusters not currently shown).  To find
which phase actually costs, `realignSpikes` now emits one line per cluster —
`setup / compute / writeback / total` ms — when `NS3_REALIGN_TIMING=1` is set.
No-op (one cheap `QElapsedTimer` pair) otherwise.

**Klusters — PCA-Center Align All: PCA basis cached across the batch.**
`realignSpikes` re-read the PCA basis (`.pca`/`.pcaD`: header + per-channel means
and eigenvectors) from disk on every call.  A batch runs over one group whose
basis never changes, so this was N redundant reads.  The loaded basis is now
cached on the document, keyed by path + file mtime so it self-invalidates if the
basis is regenerated.  Scope note: the basis is the *only* batch-invariant part
of setup — each cluster's waveforms/timestamps are fetched by direct `fseeko` to
that cluster's own spike offsets, not a full-file scan, so they cannot be
hoisted; the win is bounded by basis-read time (small for compact bases, larger
for full-waveform multi-channel ones).

**Klusters — PCA-Center Align All: the slowdown was the curation log, not I/O.**
`NS3_REALIGN_TIMING` (with the per-phase + inter-cluster-gap breakdown added this
session) showed the realign itself is ~2 ms/cluster; the flat ~130 ms per cluster
and the steadily growing inter-cluster gap were both the curation snapshot.
`snapshotClusters()` — invoked by `logBefore` *and* `logAfter`, so twice per
cluster — runs `computeAllCentroids()`, a full O(all-spikes × dims) pass over the
entire session, only to populate the snapshot's nearest-neighbor field.  Over
~1000 clusters that is ~2000 whole-dataset scans.  Fixed by logging the whole
Align-All as a single curation action: `beginRealignBatchLog(clusters)` takes one
"before" snapshot, the complete/abort paths take one "after" snapshot, and
`realignSpikes` skips its per-cluster `logBefore`/`logAfter` while the batch
suppress flag is set (set before any worker starts, cleared after the last, so the
sequential workers never race).  `computeAllCentroids()` now runs twice per batch
instead of twice per cluster; undo treats the Align-All as one action.  Diagnosis
also added opt-in per-phase realign timing (setup/compute/writeback, then a
writeback sub-split and an inter-cluster gap, all via `NS3_REALIGN_TIMING`) and a
per-batch PCA-basis cache.

 Documented why "push the
whole Align-All onto the GPU" is bounded: even the existing GPU path is
CPU-read → GPU-compute → CPU-read/apply, and only the (spike × candidate)
PCA-energy evaluation runs on the device — the `.fil` window reads and the
re-read/apply are disk I/O that stays on the CPU.  The design specifies a batched
dispatcher API (`refineBatch` + `RefineBatchDesc`), a one-block-per-spike kernel
over all clusters concatenated, host-side chunking/double-buffering, a CPU-batched
fallback (validatable without a GPU), and a `G==1` correctness oracle.  Whether it
is worth building depends on the `NS3_REALIGN_TIMING` breakdown: it accelerates
the *compute* phase only, not disk I/O or the writeback/sort path.  See
`doc/design/realign-gpu-batch.md`.


A maintenance + investigation session spanning Klusters interaction bugs, a
threading race, build-system optimization hygiene, and the first piece of the
cluster data-model rework.

**Klusters — cluster palette Up/Down skipped rows.** Up/Down navigation chose
the geometrically nearest item by *column* first, breaking ties by row.  Because
palette items are cluster-number labels of differing widths on a free-flow grid,
their visual-rect centers do not align into columns, so a single keystroke could
jump several rows.  Reworked to pick the adjacent *row* first, then the nearest
column within it — movement is now provably one row.  Left/Right (list order)
and the wrap-around path are unchanged.

**Klusters — data race in min/max recomputation.** `Data::minMaxDimensionCalculation`
(run on `minMaxThread`) snapshotted the spike table into a block-scoped local
that was destroyed immediately, then read the *live* `spikesByCluster` in its
inner loop using offsets from the cluster-map snapshot.  A concurrent pointer
swap from a reassignment (`prepareUndo`) or undo/redo could make offsets and
data come from different tables — a stale result or an out-of-bounds read.  Now
the spike-table snapshot is held for the whole scan and read in place; the
worker runs entirely on private copies.  No other worker thread reads these
tables directly (they use the mutex-guarded dict + status-flag protocol).

**Klusters — redundant `minMaxThread` wait-loops.** The eleven
`while(!minMaxThread->wait()){}` join sites in `data.cpp` were dead loops around
a blocking `QThread::wait()` (Forever deadline never returns false); replaced
with a plain `wait()`.  Behavior-preserving.

**Build system — `-ffast-math` / `-march=native` made explicit per-target
opt-ins.** Investigation found the global flag block in
`cmake/ZenOptimizations.cmake` applies flags via
`set(CMAKE_<lang>_FLAGS_<cfg> … CACHE STRING … FORCE)`, which is a no-op (a
normal-variable shadow keeps the default), so the intended global
`-march=native` / `-funroll-loops` / `-ffast-math` were **never reaching the
compiler** — only per-target flags (KiloKlustaKwik, a couple of plugins, CUDA)
took effect; LTO (set via `CMAKE_INTERPROCEDURAL_OPTIMIZATION`) was verified to
be genuinely active.  Rather than silently flip everything global (non-portable
binaries), `-ffast-math` and `-march=native` are now opt-in `INTERFACE` targets
(`ns_fast_math`, `ns_native_arch`): CXX-only, Release/RelWithDebInfo only, gated
on `NS_ZEN_OPT`, always defined.  `ns_fast_math` is wired into the KlustaKwik
numeric core; `ns_native_arch` into the self-contained CPU signal-processing
plugins (extractspikes/reextract/refeaturize variants, denoiseuniform,
medianfilter, drifttracker).  `process_pca*` is deliberately excluded (its math
is in GSL — recompiling the plugin does nothing — and it is reproducibility-
sensitive).  Measurement on real kernels: `-march=native` alone is a modest,
loop-shape-dependent win (~5% on the denoise kernel, ~2.5× on long FP reduction
chains); the large speedups need `-ffast-math` (reassociation), which is left
opt-in for the numerically-insensitive kernels only.  The broken global
mechanism is documented in `ROADMAP.md` and left unfixed pending a portability
decision.

**Roadmap — performance and data-model debt recorded.**  Added a
performance/optimization section (profiling-first plan, memory-bandwidth
reduction via `float`-where-safe and pass fusion, V-cache blocking for
reuse-heavy drift/PCA math, GPU feed path, OpenMP scaling, PGO) and a Klusters
data-model debt entry.

**Klusters — `SpikeAssignment` sparse-set store (prototype, not yet wired).**
Cluster reassignment is currently O(total spikes): every edit rebuilds the whole
`spikesByCluster` table and pushes a full table copy onto the undo stack.  Added
a header-only, Qt-free `SpikeAssignment` (`clusterOf[]` label array == the `.clu`
column, packed per-cluster `members` sparse set, `slotOf[]` back-index) giving
O(spikes moved) edits and delta-based O(moved) undo/redo, plus a standalone test
(randomized equivalence vs an independent reference, invariant checks,
undo-depth trimming, and an O(moved)-vs-O(N) perf demonstration: ~50× at 5M
spikes vs a conservative full-copy baseline).  Built alongside the existing
model; integration into `Data` (replacing the table and the reader-thread
concurrency rework the in-place model requires) is deferred to the real build,
where it must be validated for bit-identical `.clu` output.

A companion `SelectionGrid2D` (also header-only, Qt-free, with a standalone
test) addresses the *other* half of reassignment latency — the O(spikes-in-
edited-clusters) region-test scan that identifies which spikes a lasso selects.
It is a uniform grid over the displayed 2D projection (rebuilt only when the
dimension pair changes) that tests only points in cells overlapping the polygon
bounding box; the test validates it returns exactly the brute-force result over
300 random polygons and shows a ~77× query speedup for a small lasso over 2M
points.  Predicate parity with `QRegion::contains` is a wiring-time concern,
documented in-header.

**Klusters — error matrix now highlights selected pairs.** `ErrorMatrixView`
already supported multi-pair selection (Ctrl-click to add/remove), but
`drawMatrix` drew no highlight, so the selected cells were invisible against the
probability colors — making it hard to see and curate pairs on a dense matrix.
Added a yellow cosmetic-pen outline for every pair in `selectedPairs`, placed by
the same world-coordinate cell layout the matrix uses (column = `pair.first`,
row = `pair.second`); a cosmetic pen keeps the outline a constant 2 px at any
zoom, and pairs whose cluster no longer exists are skipped.

**Klusters — template matrix multi-select + per-pair highlight.** The template
matrix was single-pair: a click set one `selectedA`/`selectedB` and only that
one cell was outlined.  It now accumulates a selection the same way the error
matrix does — Ctrl-click toggles a pair in/out (rebuilding the shown-cluster set
from what remains on toggle-off), a plain click replaces it with one pair — and
`drawMatrix` outlines every selected pair, not just the most recent.  This backs
the group-by-`G` workflow: the selected pairs' clusters are added to the active
view and `G` (a window-level shortcut) groups them, so no Apply step is needed
for grouping.  `selectedA`/`selectedB` still track the most-recent pair, so the
threshold slider / Apply single-pair refinement tool is unchanged.

All three matrix-view changes are syntax-checked against Qt headers but not yet
visually verified — this environment has no Qt6 build or display.

**Klusters — error matrix pan/zoom.** The error matrix descends from
`BaseFrame` (world-coordinate `ZoomWindow`) but had overridden the press handler
to repurpose clicks for pair selection, which also disabled the inherited
rubber-band zoom — leaving no way to magnify a dense matrix to see and pick
pairs.  Pan/zoom now drives the existing `ZoomWindow` without taking the
selection click, mirroring the template matrix's gestures: Ctrl + Left-drag pans
(armed on press, engaged only past a 3 px threshold so a quick Ctrl-click still
reaches the Ctrl-add selection path; the world delta is the difference of two
`viewportToWorld` conversions so the grabbed point tracks the cursor), Ctrl +
wheel zooms around the cursor, and double-click resets to the full matrix
(restoring `BaseFrame`'s standard gesture).  Plain click is unchanged.

**Build — Qt6 audit + dead Qt5 shim removal.**  Verified the whole C++ tree is
on Qt6: every `find_package` is Qt6, every link target is `Qt6::`, resources use
`qt_add_resources`, and there are no qmake `.pro`/`.pri` files.  A scan of all
585 source files found no Qt6-removed APIs in use — `QRegExp` appears only in a
comment, `setResizeMode(QListWidget::Adjust)` and `QLabel::setMargin` are the
still-valid `QListView`/`QLabel` members (checked against the Qt6 headers), every
`SkipEmptyParts` is `Qt::`-qualified, and no bare `endl` sits on a `QTextStream`.
The only Qt5 *code* artefacts were four orphaned `ECMQt4To5Porting.cmake` KDE
porting shims (klusters, libklustersshared, ndmanager, neuroscope); a whole-repo
grep confirmed nothing `include()`s them or calls their `qt5_*` macros, so they
were removed.  The remaining "Qt5" strings in the tree are provenance comments
only.  Syntax-validation for this fork now runs against real Qt6 headers (the
edited and patched Qt files — clusterPalette, data, errormatrixview,
templatematrixview — all pass a full Qt6 `-fsyntax-only` compile).

**Build — CMake pipeline audit fixes.**  A full audit of the CMake pipeline
(top-level + all subproject roots, configured end-to-end against real Qt6) drove
several fixes:

- **-march=native is now a global opt-out** (`NS_NATIVE_ARCH`, default ON)
  instead of an opt-in `ns_native_arch` target.  The project ships no binaries —
  every user builds from source — so compiling for the host ISA is the right
  default; pass `-DNS_NATIVE_ARCH=OFF` for a portable/cross build.  This also
  retired the dead `set(CMAKE_<LANG>_FLAGS_<CFG> … CACHE … FORCE)` block (a
  verified no-op) in favor of a working `add_compile_options()` (C/CXX,
  Release/RelWithDebInfo, GNU/Clang-family).  `-ffast-math` stays opt-in via
  `ns_fast_math` (it perturbs results, not just portability).
- **Optional converter plugins degrade gracefully.**  `process_extractleds`
  (FFmpeg) and `process_aomconvert` (HDF5) used hard `REQUIRED` dependency checks
  while being added unconditionally, so a single missing optional dependency
  aborted the whole monorepo configure.  They now probe quietly and skip with an
  informative message (mirroring `process_resample`).
- **nphys-data** (data-only) is declared `LANGUAGES NONE` so it no longer forces
  a compiler, and its `CPack` include is guarded to standalone builds.
- **kiloklustakwik** defers `CMAKE_CUDA_ARCHITECTURES` to the top-level value
  (incl. native auto-detect) when set, instead of overriding it with a different
  list.
- **Standardisation:** `cmake_minimum_required` unified to `3.22...3.31` across
  the top-level and every subproject root; the inert
  `ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE` keywords in the `NS_INSTALL_DEPS`
  `execute_process` calls replaced with `COMMAND_ECHO STDOUT`.

Follow-up (now resolved, see below): four targets hardcoded `-march=native`
directly in their own flag lists, so they stayed native even under
`NS_NATIVE_ARCH=OFF`.

**Build — GPU-backend detection deduplicated.**  The CUDA architecture list, the
oneAPI/icpx search hints, the nvcc location search, and the pre-`project()` icpx
compiler auto-selection were duplicated between the monorepo top-level and the
kiloklustakwik subproject.  Factored into a new `cmake/GpuBackends.cmake` module
(`ns_gpu_find_cuda_compiler`, `ns_gpu_select_cuda_arch`,
`ns_gpu_select_sycl_compiler` + `NS_GPU_CUDA_ARCH_FALLBACK`, `NS_GPU_SYCL_HINTS`);
the two consumers shed ~110 net lines, with the logic extracted verbatim so
behavior is unchanged.  (klusters was subsequently folded in too — see below.)
The GPU-enabled probe branches (nvcc found, native-arch, icpx compiler switch)
are not exercised in a toolkit-less sandbox and should be verified on a GPU
toolchain.

**Build — klusters folded into the GPU-backend module.**  Since klusters is only
ever built within the repo, it now includes `cmake/GpuBackends.cmake` and its
SYCL probe uses `NS_GPU_SYCL_HINTS`, removing the last duplicated copy of the
oneAPI/icpx hint list.  klusters keeps its own SYCL control flow; only the hint
data is shared.

**Build — removed redundant hardcoded `-march=native`.**  With `-march=native`
now applied globally (gated on `NS_NATIVE_ARCH`), the per-target hardcoded copies
were redundant and defeated portable builds.  Removed them from the CXX/HIP/SYCL
`target_compile_options` of process_aomconvert, process_decomposecollisions,
process_resample, process_extractemg and kiloklustakwik (six sites) — all
CXX-language compiles the global already covers.  The vendored libsamplerate
ExternalProject (its own autotools `./configure`, not reached by the global) now
gates `-march=native` on `NS_NATIVE_ARCH` explicitly.  A portable build
(`-DNS_NATIVE_ARCH=OFF`) now has zero `-march=native` in any flags.make (was 4);
the default build still applies it to all 39 C/CXX targets via the global.

**Build — removed the vestigial `ns_native_arch` shim.**  Once `-march=native`
went global, `ns_native_arch` survived only as an empty INTERFACE target so the
plugins' opt-in links kept resolving.  With native now sourced entirely from the
global mechanism, the shim, the ten guarded no-op `target_link_libraries(…
ns_native_arch)` links, and their now-inaccurate comments were all dead weight
and have been removed (`ns_fast_math` is unchanged).  No references to
`ns_native_arch` remain.

**Test — opt-in regression suite (ctest).**  Wired the previously-unbuilt unit
tests into CTest and added a build-system invariants check, gated behind
`NS_BUILD_TESTS` (default OFF; addresses the audit's missing-test-wiring
finding).  Covers the kiloklustakwik algorithm modules (adapt_model,
cluster_hull_split, proxy_isi, wave_knn_split, xcorr_match, refine_logic,
klusters_realign) and the Qt-free klusters data structures (SelectionGrid2D,
SpikeAssignment), plus `build_invariants` — a static `cmake -P` check asserting
the CMake-audit invariants (NS_NATIVE_ARCH global opt-out, `ns_native_arch`
removed, `GpuBackends.cmake` present, graceful optional-dep skips, standardized
`cmake_minimum_required`, nphys-data `LANGUAGES NONE`, no hardcoded
`-march=native`).  10/10 run tests pass; `kk_test_per_channel_split` is
quarantined (`DISABLED`) for a pre-existing 1/30 assertion failure surfaced by
wiring it up.  See `tests/README.md`.

**Klusters — nudge jumped after alignment (stale `.res`).**  After any alignment
step, nudging a cluster moved its timestamps by more than one sample.
`KlustersDoc::realignSpikes` re-extracted each spike's `.spk` waveform at the
realigned window `extTs = oldTs + cumShift`, set the `.fet` timestamp column and
the in-memory timestamp to `extTs`, but wrote the *original* pre-shift timestamp
to `.res.pending` — so the on-disk `.res` was `cumShift` out of step with
`.spk`/`.fet`/memory.  `nudgeClusterTimestamps` then read that stale base and
added ±1, producing a jump of `(delta − cumShift)`.  The write loop now stores
`extTs` to `.res`, matching the nudge invariant's own self-check.  Trade-off:
`.res` event times now shift by `cumShift` (≤ maxShift) as intended; preserving
original detection times for correlograms would instead require a window-aware
nudge.

**Klusters — "PCA-Center Align All" honours the Top-Channels spin box.**  The
whole-document batch hardcoded `--topchannels 2`, ignoring the
`realignTopChanSpinBox` value the single-cluster realign already uses (via
`buildRealignArgs`).  It now reads the spin box (0 = all channels); `--pca-refine`
stays forced since the action is defined as PCA-centered.  Menu label, tooltip,
confirmation dialog and batch log header reflect the dynamic count.

**Klusters — parallelised the CPU realign fallback.**  The PCA-refine CPU path
(taken with no GPU, below the GPU spike-count threshold, or above the working-
buffer cap) ran the per-spike best-shift search single-threaded.  Each spike is
independent — it reads `.fil` and writes only its own `cumShift[i]`/`wavBuf[i]`
slot — so the body is now a `refineSpike` lambda dispatched under OpenMP with one
`.fil` handle and one scratch pair per thread (no shared file position or buffer
contention), `reduction(+:nRefined,nClamped)`, `schedule(dynamic,16)`, gated on
`N≥64` and >1 thread.  A serial fallback (identical results) runs when OpenMP is
absent or per-thread handles can't open.  `OpenMP::OpenMP_CXX` was already linked
to the klusters target; no CMake change.

**Klusters — overlap-aware denominator in template / auto-merge xcorr.**  The
normalized template xcorr (`tmNormXcorr`, and its verbatim duplicate `normXcorr`
in `autoMerge.cpp`) divided every lag by the full-length norms
`√(Σa²·Σb²)` computed once.  At nonzero lags the edge samples are dropped from
the numerator, so the dot product summed over fewer terms than the denominator
assumed — systematically under-counting off-center alignments.  Both copies now
accumulate the dot product and both squared norms over the same overlapping
window inside the lag loop, so each lag is a true cosine over the overlap.  Lag 0
and identical waveforms still give 1.0.  Note: this shifts matrix values at
off-center peaks, so auto-merge thresholds calibrated on the old metric may need
re-checking.

**Klusters — optional Pearson (mean-subtracted) template xcorr.**  An opt-in
Display-prefs checkbox (`templateXcorrPearson`, default off, persisted) switches
the normalized xcorr from cosine similarity to Pearson correlation: each
waveform's overlap-window mean is removed before normalizing (single pass via the
centering identity), so a shared DC offset no longer inflates the score.
`meanSubtract=false` is byte-identical to the cosine path.  All three consumers
(template matrix, pair-xcorr, auto-merge) read the flag once and pass it through,
so the matrix, pairwise xcorr and auto-merge stay consistent.

**Klusters — auto-merge pair scoring is cancellable + shows progress.**  The
O(nClusters²) pairwise xcorr in `computeProposals` ran with no `wasCanceled()`
check and no `processEvents()`, freezing the modal dialog with an unresponsive
Cancel for the whole scoring phase on large all-active runs (~1000 clusters).
The progress range is extended to `2·nClusters+2` and the outer scoring row loop
now polls cancel / advances the bar / pumps events once per row (the inner loop
stays the cancel granularity).  Interim fix; an async-thread migration remains
the longer-term option noted in the file header.

**Klusters — auto-merge median mode no longer truncates 32-bit samples.**  In the
median path, per-spike waveforms were stored in an int16 buffer and 32-bit
samples narrowed with `static_cast<int16_t>` before the per-element median, so on
a 32-bit recording (`isRecordingTwoBytes()==false` ⇔ `nbBits==32`, int32 `.spk`)
any sample beyond ±32767 was corrupted and median templates disagreed with mean
mode (which sums into a `double`).  The buffer is now int32 — exact for both the
int16 and int32 read paths — with the median collection vector widened to match;
median-mode memory for the subsampled spikes doubles, bounded by `medianK·nPts`.

**Klusters — shared `.spk` reader; analysis path committed to int16 (Design A).**
The three template/xcorr consumers (TemplateMatrixThread, PairXcorrThread,
auto-merge) each had their own copy of the spike read: `fseeko` + `fread` + a
2-vs-4-byte branch on `isRecordingTwoBytes()` + sample-major→channel-major
de-interleave.  The whole ndmanager-plugins extractor family writes `.spk`/`.spkD`
as int16 regardless of acquisition `nBits` (and the `.fil`/`.dat` inputs are
int16 too), so the 4-byte branch was dead for any data the toolchain produces —
and for a nominally-32-bit session it would have misread the int16 `.spk`.
Replaced the three copies with one `tmReadSpikeFloat()` that reads int16 and
returns a channel-major float buffer; the de-interleave indexing is verbatim, so
for real (int16) data the result is byte-identical.  The median-mode buffer that
the truncation fix had widened to int32 is now float (exact for int16, consistent
with mean mode).  This is the analysis-side of "cast at the I/O boundary"; the
core load path (`Data::loadClusters` sizing, `WaveformData<long>`) still branches
on `nBits` and is a separate, higher-blast-radius change.

**Klusters — error-matrix wheel zoom anchors the cursor.**  `ErrorMatrixView::
wheelEvent` passed the world point under the cursor straight to `ZoomWindow::
zoom(factor, center)`, whose second argument is the new window *center* — so each
Ctrl+wheel notch recentred the view on the cursor, jumping whatever was under the
pointer to the middle of the view.  Compute the center that keeps the pivot fixed
(`cNew = pivot − (1/factor)·(pivot − cOld)`) so the point under the cursor stays
put; `correctWindow()` still clamps to the matrix bounds.  Panning was already
correct.  (The sister TemplateMatrixView already did cursor-anchored zoom.)

**Klusters — O(1) exact cluster lookup in the palette (perf + correctness).**
`ClusterPalette::selectItems`, which runs on every selection sync, resolved each
requested cluster with `findItems(QString::number(id), MatchStartsWith)`: O(n)
per cluster (O(k·n) overall, sluggish at thousands of clusters) and matched by
label *prefix*, so selecting id 10 could land on "100 - noise".  It now builds a
cluster-id → item `QHash` once and resolves each id exactly in O(1).

**Klusters — O(1) ignore lookup in the error-matrix paint.**  The nbClusters²
cell loop called `ignoreClusterIndex.contains()` (linear over a `QList`) twice per
cell — millions of linear scans per repaint at high cluster counts.  Precompute a
flag vector indexed by cluster index and test it in O(1).

**Klusters — render the error matrix as one image, not per-cell drawRect.**
`drawContents` issued one `painter.drawRect()` per cell (≈9M calls at 3000
clusters) on every REDRAW, i.e. on every pan move and zoom notch — the dominant
cost behind the matrix feeling slow at high cluster counts.  Fill an 8-bit
indexed `QImage` (one pixel per cell, `colorMap` as the color table,
diagonal/ignored → black) by scanline and blit it once with `drawImage` scaled to
the matrix world rect (`SmoothPixmapTransform` off for crisp cells).  Color
computation is unchanged, so the matrix is visually identical; the red
out-of-date border and yellow selected-pair outlines are still drawn over it.

</details>

## 2026-05-16 — Cluster-mean alignment stack + .res/.spk/.fet consistency

<details>
<summary>Details</summary>

Consolidates patches 64, 69–87, spanning several weeks of work on
post-clustering spike alignment and the disk-commit pipeline that
follows it.  Detailed per-patch entries live in
`src/kiloklustakwik/CHANGES.md`; what follows is the integrating
summary.

**Symptom.** After `KlustaKwikExp` completed on a stderiv session,
cluster mean waveforms in Klusters showed a residual ±3 sample
temporal dispersion despite the alignment phases reporting success.
The dispersion was visible in the cluster-mean view, in the
template-matrix mismatch heat-map, and (subtly) in cluster-quality
metrics.

**Root causes — three independent.**

1. **Template never centered before xcorr** (patch64).
   `RunPhase2bMode3Chunk` built the per-cluster mean template, then
   ran xcorr of each spike against it — but never shifted the mean
   so its peak landed on `PeakSampleIndex`.  xcorr therefore pushed
   every spike toward the mean's *natural* peak, which itself was
   off-`PeakSampleIndex` by whatever residual detection jitter the
   chunk's spikes happened to share.  Spikes whose required shift
   exceeded `MaxTimeShift` were silently rejected and stayed at
   their detection positions, leaving cluster spikes scattered up
   to ±5 samples around their cluster mean.

2. **Live-nudge in Klusters had a raw-source ambiguity** (patches
   69, 72).  Both Klusters' `realignSpikes` and KKExp's Phase 2b m3
   alignment block did `.fil`-or-`.dat` silent fallback when picking
   the raw source for re-extraction.  When `.spk` was extracted from
   `.fil` but `.fil` had been deleted or recomputed with different
   filter settings, the code silently re-extracted from `.dat`,
   accumulating mixed-source content in `.spk` on every nudge.
   patch69 added an RMS-based verifier to Klusters; patch72 ported
   it into KKExp.

3. **`WritePhase15Checkpoint` wrote `.spk`/`.fet` but not `.res`**
   (patch85).  When alignment generated non-zero cumulative shifts,
   the checkpoint re-extracted shifted waveforms into `.spk`
   (anchored at `rawTs + sh - PeakSampleIndex`) and wrote shifted
   timestamps into the `.fet` last column, but **never updated
   `.res` itself**.  Klusters then displayed `.spk` windows whose
   peaks aligned with each other but whose stored timestamps
   contradicted the window content — visible as the ±3-sample peak
   scatter.

**Fix path — phased.**

`RunPhase2bMode3Chunk` was rewritten as an iterative xcorr loop
(patches 71–74) that pre-aligns the template to `PeakSampleIndex`
before correlating, accumulates cumulative shifts in `m_cumShift[]`,
re-projects features against the pre-shifted PCA basis at each
iter, and emits a post-pass `.res` monotonicity warning when adjacent
spikes' new timestamps cross.  An RMS-based raw-source verifier
guards the entry to the alignment block; verification failure aborts
this chunk's alignment without polluting `m_cumShift` from prior
phases.

Two `AlignPcaCenter` modes were added on top of the xcorr loop
(patches 83–84) to address residual dispersion that xcorr alone
couldn't reach:

- Mode 1: post-xcorr PCA-centering refine pass.  After the iter loop
  converges, for each spike sweep shifts in `[-maxShift, +maxShift]`
  and minimize the squared distance from the spike's PCA
  representation to the cluster mean's PCA centroid.  Writes
  `m_cumShift[]`, so the disk commit captures it.

- Mode 2: in-memory circular-shift PCA-centering replacement.
  Replaces the entire xcorr iter loop with a circular-shift sweep,
  picking each spike's argmin distance directly.  Does NOT write
  `m_cumShift[]` — purely diagnostic / corrective in memory.

`WritePhase15Checkpoint` was made `.res`-consistent (patch85): after
rewriting `.spk` and `.fet`, the checkpoint also reads the original
`.res`, writes a `.pending` companion with `rawTs + sh` for each
shifted spike, then atomically renames it into place.  All three
files now move together via the `.*.pending` mechanism.

**Residual dispersion after patch85 — stderiv sample-0 error**
(patches 86, 87).  Even with `.res`/`.spk`/`.fet` consistent on disk,
the user's stderiv pipeline still showed sub-sample-level dispersion
in cluster means.  Tracked down to `WritePhase15Checkpoint`'s
per-spike re-extract: for the stderiv variant, it has no cross-spike
state, so the temporal first-difference at the s=0 sample of every
shifted spike uses `prev_sdiff = 0` instead of the actual previous
sample's spatial derivative.  This injects a sample-0 error in every
shifted `.spkD` window — invisible per-spike but accumulates into
cluster-mean dispersion.

Fix: add a `-R` mode to all three `process_extractspikes` variants
(patch86) that reads existing `.res.<grp>` as authoritative
timestamps instead of detecting, then mmaps `.fil` and re-extracts at
exactly `(ts - timeBefore)` for each.  The stderiv variant reads one
extra raw sample at `(ws - 1)` to compute the correct `prev_sdiff`
for the s=0 temporal-diff.  No cross-chunk state needed because
mmap-and-jump is per-spike independent in this read pattern.

KKExp now invokes the appropriate extractor in `-R` mode
automatically at the end of `TimeShiftFinalize` (patch87) when
non-zero shifts were committed.  Pipeline detection via
`m_timeShiftBasis.isStderiv`; sdiff users set
`KKEXP_REEXTRACT_TOOL` explicitly.  Auto-invoke is disabled by
`KKEXP_AUTO_REEXTRACT=0`.

**Klusters polish — separate track, same dates.**  patches 75, 79–82
addressed UX issues surfaced by the alignment debugging work:

- Time dimension excluded from auto-selected features in
  `slotRecluster` (patch75) — including the timestamp column as a
  clustering feature was causing drift-induced spurious splits in
  recluster.
- Mean-subtracted subdimensional recluster mode added for
  single-cluster selection (patches 76–78), exposing sub-structure
  the canonical PCA basis can't separate.  Three patches because
  the first revealed two integration bugs (`reclusteringSpikesByCluster`
  not populated, and a return-value enum collision with
  `OpenSaveCreateReturnMessage`).
- Error and Template matrices now auto-show on document open (patch79),
  removing four mouse clicks from the standard curation entry path.
- Template matrix gained `Ctrl+drag` pan + `Ctrl+wheel` zoom
  (patch80) for sessions with cluster counts that overflow the
  default cell size.
- Session YAML is now staged at the recluster temp basename
  (patch81) — `KlustaKwikYaml.cpp` was looking up `<tempBase>.yaml`
  which never exists, silently defaulting `SamplingRate` to 20000.
- A PCA-projection-energy-maximizing alignment refine pass was added
  to `realignSpikes` (patch82), opt-in via a checkbox in the realign
  dialog.  Per-spike fresh `.fil` re-extract per candidate shift,
  argmax of energy in the basis.

### Files changed

| Area | Files |
|---|---|
| KKExp alignment + disk commit | `src/kiloklustakwik/KK.cpp`, `KK.h`, `KlustaKwik.cpp` |
| extractspikes `-R` mode | `src/ndmanager-plugins/src/process_extractspikes/`, `..._sdiff/`, `..._stderiv/` |
| Klusters interactions | `src/klusters/src/klusters.cpp`, `klustersdoc.cpp`/`.h`, `data.cpp`/`.h`, `templatematrixview.cpp`/`.h`, `spikerealigndialog.cpp`/`.h`, `prefgenerallayout.ui`, `configuration.cpp`/`.h` |
| Live-nudge raw-source verify | `src/klusters/src/klustersdoc.cpp` |

### Environment knobs introduced

| Variable | Effect | Default |
|---|---|---|
| `KKEXP_AUTO_REEXTRACT` | `0` disables patch87 auto-invoke | unset (= enabled) |
| `KKEXP_REEXTRACT_TOOL` | Override tool binary name (e.g. `process_extractspikes_sdiff`) | auto from `isStderiv` |
| `KKEXP_REEXTRACT_SDIFF_ORDER` | Pass `-d <0..3>` to stderiv tool | `3` (ALLPAIRS) |
| `KKEXP_REEXTRACT_VERBOSE` | Pass `-v` to the tool | unset |

### CLI knobs introduced

`process_extractspikes`, `process_extractspikes_stderiv`,
`process_extractspikes_sdiff` all accept:

- `-R` — skip detection; read `<basename>.res.<grp>` and re-extract
  waveforms at those exact timestamps.  Threshold / refractory /
  peak-search args ignored in this mode.

KKExp recognises:

- `-AlignPcaCenter <0|1|2>` — 0 disables PCA-centering (xcorr only);
  1 adds it as a refine pass after xcorr (writes `m_cumShift`); 2
  replaces xcorr entirely with in-memory circular shifts (no
  `m_cumShift` writes).

### Migration notes

For sessions clustered with pre-patch85 KKExp where the dispersion
is already on disk: the cleanest recovery is to apply all of 85–87
then re-run KKExp in mode 1 (`-AlignPcaCenter 1`).  Mode 1 writes
`m_cumShift`, which triggers `WritePhase15Checkpoint`, which now
keeps `.res`/`.spk`/`.fet` consistent, which the patch87 auto-invoke
then refreshes from `.fil` via the patch86 `-R` path.  Mode 2 alone
won't fix existing-disk dispersion (it never writes to disk by
design).

### Known limitations

- After re-extracting `.spkD`, the `.pcaD` basis is still computed
  from the pre-alignment dispersed data.  For maximally-clean
  features, run `ndm_pca_stderiv` + recompute `.fetD` after the
  KKExp run; this is not chained automatically.
- The sdiff pipeline (`.spk` produced by `process_extractspikes_sdiff`)
  cannot be auto-detected by KKExp — it shares the `.spk` extension
  with the vanilla pipeline.  sdiff users must export
  `KKEXP_REEXTRACT_TOOL=process_extractspikes_sdiff`.

</details>

## 2026-04-27 — ndm_start root + `.ndm.<n>.pipeline` files + empty-graph default

<details>
<summary>Details</summary>

Restores the editable Pipeline Designer (rolled back the toggle-only
view shipped on 2026-04-26) and finishes the design with three changes:

**1. Sticky `ndm_start` root.** Every graph has `ndm_start` pinned at
position 0. Undeletable, indegree-0, single-instance, gold "★ ROOT"
badge in the header. The legacy seven flag parameters (`wideband`,
`events`, `video`, `concatenation`, `spikes`, `lfp`, `clean`) live
on the root node's params for backward-compat fallback.

**2. Pipeline files in their own format.** The graph is no longer
serialized inside the session YAML's `programs:` block — instead it
lives in a sibling file:

```
session.yaml                             ← session schema (unchanged)
session.ndm.default.pipeline             ← auto-loaded on document open
session.ndm.best.pipeline                ← named variants via Save As
session.ndm.experimental.pipeline
```

The `.ndm.` infix namespaces our pipeline files so they don't collide
with `.pipeline` files that might be generated by other tools (Klusters,
snakemake, nextflow, etc.) in the same session directory.

New File-menu actions:
- **Save Pipeline** (Ctrl+Alt+P) — writes `<session>.ndm.default.pipeline`
- **Save Pipeline As…** (Ctrl+Alt+Shift+P) — prompts for a name, writes
  `<session>.ndm.<n>.pipeline`
- **Load Pipeline…** — file dialog filtered to `*.pipeline`

The Pipeline tab toolbar also has matching Save / Save As buttons.

Sanitisation rules: pipeline names are lowercased, whitespace collapses
to underscore, all characters outside `[a-z0-9_-]` are stripped.
Existing-file overwrite prompts on Save As.

**3. Empty-graph default.** Loading a session that doesn't already
have a saved pipeline (no `programs[0]==ndm_start` entry) now starts
with an *empty graph containing only the `ndm_start` root*, instead
of being pre-populated with every plugin from the YAML's parameter
pool. The user builds the pipeline from scratch via the palette;
their work is saved separately to `<session>.ndm.default.pipeline`.

**Bash dispatcher priority:**
1. `<template>.ndm.${NDM_PIPELINE:-default}.pipeline` (env-var
   selectable for CI)
2. In-YAML graph (`programs[0]==ndm_start`) — legacy form, kept for
   backward compat
3. Hard-coded flag-driven `do_*` sequence — original ndm_start,
   kept for sessions with no graph at all

Per-plugin failures are warnings, not aborts. A graph reference to a
plugin not on PATH → warning + skip. Existing session YAMLs continue
to work unchanged.

See `doc/design/ndm-start-root.md` for the full design and
`doc/ndmanager/README.md#pipeline-tab` for the operational
walkthrough.

</details>

## 2026-04-27 — ndm_start as the graph root (replaced)

<details>
<summary>Details</summary>

*Replaced by the entry above on the same day.* This earlier draft
serialized pipelines inside the session YAML's `programs:` block;
the final design moved them to separate `<session>.ndm.<n>.pipeline`
files so the graph is independent of the session schema and named
variants can coexist.

</details>

## 2026-04-26 — graphical Pipeline tab in ndmanager (superseded)

<details>
<summary>Details</summary>

*Superseded by the 2026-04-27 entry above.* The toggle-only view
shipped here was rolled back in favor of the editable node-graph
designer that already existed in the repo (commit `4e0de6e`),
extended with sticky-root semantics for `ndm_start`. The
`PipelinePage` class introduced by this entry was deleted; the
parameter-page signal additions (`parameterValueChanged`,
`setParameterValue`) were retained for potential future use.

</details>

## 2026-04-26 — graphical Pipeline tab in ndmanager (original)

<details>
<summary>Details</summary>

New **Pipeline** entry in ndmanager's parameter tree showing
`ndm_start` and its seven dispatch branches (`wideband`, `events`,
`video`, `concatenation`, `spikes`, `lfp`, `clean`) as a clickable
node graph. Each branch corresponds to one of the boolean flags
read by `ndm_start`'s bash, with the sub-step plugins it invokes
(`ndm_resample`, `ndm_extractspikes`, `ndm_pca`, …) shown as
read-only context nodes underneath.

**Bidirectional sync.** Toggle a branch in the graph and the
matching cell in the `ndm_start` ProgramPage's parameter table
updates immediately; edit the table cell directly and the graph
follows. Saving the document writes the new flag values under
`programs[ndm_start].parameters` — no schema changes.

**Default handling.** Sessions that don't include `ndm_start` in
their `programs:` block show the graph in defaults-only read-only
mode. Adding `ndm_start` via Plugins → Add binds the page
automatically. Toggling a flag that was relying on the script
default (e.g. `clean` not present) appends an explicit row so the
next CLI run sees the GUI's choice.

The orchestration order itself stays hard-coded in `ndm_start`'s
bash — only the on/off flags are GUI-editable. Re-ordering would
require making `ndm_start` data-driven and is intentionally deferred.

See `doc/design/pipeline-tab.md` for the full design and
`doc/ndmanager/README.md#pipeline-tab` for the operational
walkthrough.

</details>

## 2026-04-25 — per-probe empirical KlustaKwik priors

<details>
<summary>Details</summary>

Adds `kk_build_prior.py` / `kk_resolve_prior.py` and wires them into
`ndm_subcluster_unmatched`. Lets you train empirical KlustaKwik
defaults once per probe-type-and-shank from accumulated Klusters
curation logs, then reuse them silently across sessions, animals, and
re-extraction passes.

**The workflow:**

1. Curate a few sessions on a shank in Klusters as usual. Annotate
   cluster quality (Good / Uncertain) — that gates which clusters
   feed the prior. Klusters writes `session.curation_log.<group>.jl`
   for every editing operation.
2. Run `kk_build_prior.py logs… --session-yaml … --electrode-group N
   --out-dir ~/.ndm/priors`. One YAML per shank.
3. `ndm_subcluster_unmatched` invokes `kk_resolve_prior.py` silently
   for every shank as it runs. When a prior matches, four headline
   values (`MinClusters`, `MaxClusters`, `MergeThresh`, `PenaltyMix`)
   become the defaults for `read_kk_param`. Per-session `extraInfos`
   overrides still win.
4. New animal on the same probe → same probe_id resolves the same
   prior automatically. New probe type → resolver misses; falls back
   to script defaults; build a new prior after curating.

**Probe identity:** SHA-256 of the canonicalised
`(nChannels, [{channels:sorted, nSamples, peakSampleIndex, nFeatures}…])`
tuple from the session YAML — sample rate deliberately excluded since
it's acquisition config, not probe identity. When the signature
exactly matches an installed `Template-*.yaml`, the friendly name
(e.g. `4-octrodes-32552Hz`) is used as the human-readable id;
hex-hash fallback otherwise. The hash is stored in every prior YAML
as `probe_signature_hash` for fail-loud verification.

**Best-effort everywhere.** Missing resolver, missing prior file, or
hash mismatch all fall through cleanly to script-builtin defaults.
The behavior with no priors built is exactly the previous behavior.

See `doc/design/kk-prior.md` for the full design and
`doc/workflows/empirical-priors.md` for the operational
walkthrough.

</details>

## 2026-04-22 — process_shadowcluster inflation guard + dedicated KK tier for sub-clustering

<details>
<summary>Details</summary>

**Symptom.** `ndm_reextractspikes_stderiv` on `jg05-20120316` group 7:

```
  eligible parents : 13 / 16 clusters
  new spikes       : 162365
    clu 2  (size=282)  shadow=18  +1257 new
    clu 4  (size=710)  shadow=20  +19 new
    clu 6  (size=1868)  shadow=22  +108912 new         ← 60× inflation
    clu 11 (size=4388) shadow=27  +1 new
  unmatched        : 52176
```

108 912 new spikes (67 %) force-matched into a single shadow
(`clu 6`, whose reference size was 1 868).  These spikes bypass the
unmatched bin entirely, so `ndm_subcluster_unmatched` can't touch them:
they surface in Klusters as one giant opaque shadow attached to an
otherwise unremarkable cluster.

**Root cause.** `process_shadowcluster` v1.0 did pure Mahalanobis 1-NN
with a global χ² gate (`p=0.9999`, `d²=54.234` on `df=21`).  A
reference cluster with a heavy-tailed member distribution has its
per-dim MAD × 1.4826 σ's over-estimated; the resulting ellipsoid is
over-wide and absorbs whatever falls nearby.  No downstream parameter
— KlustaKwik or otherwise — can correct this after the fact because
the absorbed spikes are tagged with the parent's shadow id before any
clustering step runs.

### Fix — three coordinated changes

**1. Inflation guard in `process_shadowcluster` (v1.1).**
New CLI args `--inflationRatio R` (default 5.0) and `--inflationCap C`
(default 500).  A parent may absorb at most
`max(C, R × |parent|)` new spikes.  Implementation:

- Pass 1 (parallel, expensive): compute `bestK/bestD²` per new spike;
  record in `tentativeBestK / tentativeBestD2`.
- Pass 2 (serial, cheap): for each over-absorbing parent, sort
  candidate spikes by `d²` ascending, keep the closest `cap_k`, demote
  the tail to the unmatched bin.

Serial cost is `O(n_assigned log n_assigned)` per over-absorbing
parent; typically 1-3 parents trip the cap per group, so the overhead
is negligible relative to the main assignment loop (nFeatDim × nEligible
distance evaluations per spike).  Verbose output now reports both
accepted and demoted counts per parent and a total-demoted summary.
Setting either knob to 0 disables the guard (preserves v1.0 behavior
bit-for-bit).

**2. Tighter default `chi2P`.**  0.9999 → 0.999.  `d²=54.234` vs
`d²≈46.80` on `df=21`.  With robust MAD σ's the 0.9999 gate proved
over-permissive in practice; 0.999 is still generous — robust-stats
literature typically uses 0.99 for outlier rejection — while being
significantly more selective.  The inflation guard is the hard
backstop; this is the per-spike gate.

**3. Caller-block tier in `read_kk_param`.**  `ndm_functions` gains
an optional `-b <caller_block>` flag.  Resolution order is now four
tiers when `-b` is given:

```
  1. spikeDetection.channelGroups[g].klustakwik.<n>    (per-group)
  2. programs[<caller_block>].parameters.<n>           (caller override)
  3. programs[ndm_klustakwik].parameters.<n>           (global fallback)
  4. built-in default                                  (safety net)
```

Tier 2 is skipped when `-b` is absent or equals `ndm_klustakwik`,
preserving backward compatibility.  `ndm_subcluster_unmatched` now
passes `-b ndm_subcluster_unmatched` at every call site (27 retrofit
sites).

### New YAML block: `programs[ndm_subcluster_unmatched]`

Populated in `templates/template.yaml` with outlier-bin-tuned defaults
that deliberately diverge from `programs[ndm_klustakwik]`:

| knob                      | ndm_klustakwik | ndm_subcluster_unmatched | rationale |
|---------------------------|---------------:|-------------------------:|-----------|
| `maxPossibleClusters`     |            500 |                     1000 | outlier bin holds dozens of low-SNR units |
| `mergeThresh`             |           42.0 |                     30.0 | below χ²(21, 0.99)≈38.9; prevents collapse of distinct low-SNR units |
| `nStarts`                 |              3 |                        5 | more CEM local minima in outlier bin |
| `phase15Iters`            |              1 |                        2 | sharper xcorr realignment in low-SNR regime |
| `subspaceRecluster`       |              0 |                        1 | split sub-units that coalesce in full 21D |
| `subspaceReclusterDepth`  |              0 |                        2 | moderate recursion depth |
| `splitRecurseDepth`       |              1 |                        1 | (unchanged — existing default is already right here) |
| `templateMatchScore`      |            0.0 |                     0.85 | waveform-similarity assist (safe in sparse bin) |
| `penaltyMix`              |              0 |                     0.25 | AIC mix counters disproportionate BIC pressure |

Users can override any subset per-session; unset keys fall through to
`programs[ndm_klustakwik]` (tier 3).

### New re-extract YAML parameters

Added to both `programs[ndm_reextractspikes]` and
`programs[ndm_reextractspikes_stderiv]`:

| name                        | default | effect |
|-----------------------------|--------:|--------|
| `reextractChi2`             |   0.999 | **CHANGED** from 0.9999 |
| `reextractInflationRatio`   |     5.0 | new — passed as `--inflationRatio` to `process_shadowcluster` |
| `reextractInflationCap`     |     500 | new — passed as `--inflationCap` |
| `reextractAutoSubcluster`   |       0 | new — when 1, `ndm_subcluster_unmatched` runs automatically after the merge |

XML descriptors (`descriptions/ndm_reextractspikes{,_stderiv}.xml`)
updated in lock-step.

### Files touched

```
src/ndmanager-plugins/src/process_shadowcluster/process_shadowcluster.cpp
src/ndmanager-plugins/scripts/ndm_functions
src/ndmanager-plugins/scripts/ndm_subcluster_unmatched
src/ndmanager-plugins/scripts/ndm_reextractspikes
src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv
src/ndmanager-plugins/descriptions/ndm_reextractspikes.xml
src/ndmanager-plugins/descriptions/ndm_reextractspikes_stderiv.xml
templates/template.yaml
```

### Backward compatibility

- `process_shadowcluster` without `--inflationRatio`/`--inflationCap`
  on the command line defaults to `5.0`/`500` (guard active).  Sessions
  that must preserve exact v1.0 behavior should set
  `reextractInflationRatio: 0` (or `reextractInflationCap: 0`) in the
  session YAML.
- `read_kk_param` without `-b` preserves the prior three-tier order
  bit-for-bit.
- Existing sessions with no `programs[ndm_subcluster_unmatched]` block
  continue to work — tier 2 is empty, tier 3 (`programs[ndm_klustakwik]`)
  supplies every parameter as before.  Only sessions regenerated from
  the new `template.yaml` or hand-edited to add the block get the
  outlier-bin-tuned defaults.
- `.clu.N` cluster-id convention (shadow-offset and unmatched-bin id)
  is unchanged.

### Expected post-fix behavior for the `jg05-20120316` g7 case

- `clu 6` shadow capped at `max(500, 5 × 1868) = 9340` new spikes
  (down from 108 912; the other ~99 572 are demoted to the unmatched
  bin).
- Unmatched bin grows from 52 176 → ~151 748, and now contains the
  population that was previously hidden inside `clu 6`'s shadow.
- `ndm_subcluster_unmatched` (auto-invoked if
  `reextractAutoSubcluster=1`, otherwise manual) runs KlustaKwik with
  the outlier-bin-tuned parameter block on that 151 748-spike bin and
  discovers its sub-structure rather than leaving it as one amorphous
  pile.
- In Klusters, the operator now sees a realistic distribution of
  low-amplitude units instead of one giant green shadow attached to
  cluster 6.

</details>

## 2026-04-22 — process_reextractspikes_stderiv writes transformed .spkD (correctness)

<details>
<summary>Details</summary>

**Value-space mismatch** between `.spkD` files produced by the main
pipeline and those produced by the re-extract pipeline:

- `process_extractspikes_stderiv` (main) writes **stderiv-transformed**
  waveforms to `.spkD.N` (spatial derivative across group channels
  + temporal first-difference with `sdiff[-1]=0` boundary).
- `process_reextractspikes_stderiv` (re-extract second pass) was
  writing **raw ADC** waveforms to the same `.spkD.N` at the new-spike
  rows, creating a file that contained two different value spaces
  interleaved — reference rows stderiv, new rows raw.

Downstream consequences:

- `process_shadowcluster::projectSpike` projects every new-spike row
  through the `.pcaD` basis unchanged.  `.pcaD` was trained on
  stderiv-transformed waveforms, so projecting raw-amplitude rows
  through that basis yields features concentrated in an arbitrary
  manifold.  The robust-Mahalanobis classifier then assigns ~all new
  spikes to a single shadow cluster, producing the observed
  "162,358 new spikes → cluster 22, one green mass at the far right
  of cluster 5" symptom in the `jg05-20120316-7` reference session.
- Klusters' waveform view reads `.spkD` bytes directly, so reference
  rows show biphasic stderiv shapes while new rows show raw-ADC
  monophasic shapes — visible side-by-side in the per-cluster
  waveform pane of the same electrode group.

### Fix

`process_reextractspikes_stderiv` Pass-2 write loop now applies the
full stderiv transform before writing to `.spkD`:

1. Spatial derivative per time sample via the already-available
   `computeSDiff` helper (ported unchanged from
   `process_extractspikes_stderiv`, identical for SDIFF_NONE /
   SDIFF_FIRST / SDIFF_LAPLACIAN / SDIFF_ALLPAIRS).
2. Temporal first-difference in-place with `sdiff[-1]=0` boundary.
   `prev[]` stores the unclamped sdiff value so subsequent
   iterations subtract from the pre-clamp value, preserving
   bit-identity with the main extractor's output.

Output layout is sample-major `[s*nChanG + ci]`, matching
`process_extractspikes_stderiv`'s `.spkD` layout exactly.  No change
to the `.res.N` write path or to the detection-stage threshold
computation.

### Rolling back previously-corrupted sessions

Any `.spkD.N` files that have been merged in-place by the buggy
version contain raw waveforms at the new-spike rows.  The script's
`.bak` preservation lets the operator revert:

```bash
mv session.spkD.7.bak  session.spkD.7
mv session.res.7.bak   session.res.7
mv session.clu.7.bak   session.clu.7
mv session.fetD.7.bak  session.fetD.7   # if present
```

Re-run `ndm_reextractspikes_stderiv` with the fixed binary after the
rollback.  Groups that have not yet been merged (no `.bak` sidecars)
are unaffected.

### Files

- `src/ndmanager-plugins/src/process_reextractspikes_stderiv/process_reextractspikes_stderiv.cpp`
  — Pass-2 write now applies the stderiv transform
- `src/ndmanager-plugins/src/process_reextractspikes_stderiv/process_reextractspikes_stderiv.h`
  — docstring corrected
- `src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv`
  — header and inline comments corrected (no behavioral change in
  bash; the binary carries the fix)

</details>

## 2026-04-22 — fetD completion, klusters focus fixes, Pipeline C nudge correctness

<details>
<summary>Details</summary>

Committed as `f2e7fd4 bugfix(klusters) fix focus issue after spike realignment`.
Despite the commit-message framing, this change set closes out three
long-standing concerns together: incomplete `.fetD`/`.spkD`/`.pcaD`
propagation inside KlustaKwik, focus-chain bugs across the klusters
cluster-operation slots, and a correctness bug where `nudge` and
`realignSpikes` silently corrupted `.fetD` on Pipeline-C sessions
(raw `.spk` + stderiv `.fetD`/`.pcaD`).

### KlustaKwik — `.fetD`/`.spkD`/`.pcaD` fallbacks beyond `LoadData`

Prior work introduced `pickInputPath` (prefer canonical, fall back to
D variant) and wired it into `LoadData`.  This session extends the
same resolution to every other session-file open inside KlustaKwik:

- `RealignChunkWaveforms` — in-place `.spk` rewrite now resolves the
  picked variant.  The `r+b` open path is unchanged; its existing
  graceful "skipping" fallback handles read-only mounts.
- `RefeaturizeFromShifts` — both the `.pca` model load and the `.spk`
  circular-shift fallback (when `.fil` is unavailable) use
  `pickInputPath`.
- Four Phase 1.6 / Phase 2 template-match mean-waveform harvests
  (`KK.cpp` ~2674, ~2750, ~3331, ~3449) — each pass opens the correct
  variant.
- `WritePhase15Checkpoint` — `.spk` and `.fet` originals now come
  from `pickInputPath`, and the `.pending` names are derived from the
  *picked* paths rather than canonical literals.  On success the
  `rename(.pending → original)` now commits back to `.spkD.N` /
  `.fetD.N` when that's what was loaded, instead of creating a ghost
  canonical file alongside the real D variant.
- Startup banner shows `(stderiv variant)` when `.fetD.N` was loaded.
- Build tag bumped to `[build 2026-04-21 fetD]`.

### ndm_klustakwik — accept `.fet.N` OR `.fetD.N`

The input-file check split the parameter-file requirement from the
feature-file requirement.  The feature-file check now accepts either
`.fet.N` or `.fetD.N`, matching what KlustaKwik itself resolves.
Stderiv-only groups no longer emit spurious "input file missing"
warnings.

### Three-tier parameter resolution — latent bug fixed

`ndm_klustakwik` referenced seven variables in its `KlustaKwik`
invocation that were never read via `read_kk_param`:
`subspaceDims`, `subspaceRecluster`, `subspaceReclusterDepth`,
`templateMatchScore`, `templateMatchIters`, `splitRecurseDepth`,
`crossChunkTemplateScore`.  Every session up to this point was
launching KlustaKwik with empty-string values for those flags and
silently relying on `param.c`'s fallback to compile-time defaults —
so per-group YAML overrides for subspace reclustering and template
matching were being ignored by the main sort.

Fixes:
- Added `read_kk_param` calls for all seven with sensible defaults
  (`subspaceRecluster=1`, `subspaceReclusterDepth=2`, `subspaceDims=3`,
  `templateMatchIters=10`, others `0`/`0.0`).
- Hoisted `read_kk_param` from `ndm_klustakwik` into `ndm_functions`
  so wrapper scripts can use identical three-tier resolution.  Passes
  `-s ndm_klustakwik` to `read_script_parameter` so tier-2 always
  reads from the single canonical program block regardless of which
  script is calling.

**User-visible effect:** subsequent main-sort runs will actually
apply subspace reclustering on groups whose YAML configures it;
cluster counts per group will likely shift.  Keep pre-patch `.clu.N`
files as `.clu.N.bak-presubspace` before re-running.

### ndm_subcluster_unmatched — full-parity sandbox KK

The sandbox KlustaKwik invocation passed only five flags
(`-MinClusters`, `-MaxClusters`, `-MaxPossibleClusters`,
`-UseFeatures`, `-MergeThresh`).  With `nStarts=1` and
`SubspaceRecluster=0` (defaults), CEM on a 186k-spike outlier bin
predictably collapsed to a single cluster, yielding
`promoted=0 noise_kept=186981`.

Rewritten invocation now passes all 27 KK flags via the shared
`read_kk_param`, matching `ndm_klustakwik` line-for-line except for
the three throw-away-session values (`-UseFeatures all`,
`-fSaveModel 0`, `-SaveIntermediates 0`).  Parity verified by
flag-set diff.

### klusters — palette focus after destructive / batch operations

Every cluster-list operation that auto-selected the next cluster was
then calling `activeView()->focusClusterView()`, which stole focus
to the 2D scatter and silently broke arrow-key navigation.  Four
slots fixed to restore palette focus instead:

- `slotMoveClustersToNoise` (`Delete` key) — `klusters.cpp:2507`
- `slotMoveClustersToArtefact` (`Shift+Delete`) — `klusters.cpp:2526`
- `slotReclusterFinished` — `klusters.cpp:3262`
- `slotRealignFinished` accept branch — removed the
  `focusClusterView()` call that was overriding the
  immediately-prior `setFocusToList()`
- `slotRealignFinished` reject branch — added symmetric palette
  focus restoration (previously left focus on whatever widget the
  modal review dialog destroyed, silently unpredictable)

Leaves existing `focusClusterView` calls untouched where the user's
next action is plausibly 2D inspection (undo/redo, group, nudge
buttons).

### klusters — nudge/realign Pipeline C correctness

`nudgeClusterTimestamps` and `realignSpikes` used a single flag
(`isStderivSession` / `isStderivRealign`) derived from
`m_origSpkPath.contains(".spkD.")` to gate *both* the `.spk` write
branch and the PCA basis + feature-reprojection branches.  These are
genuinely independent signals that only coincide in Pipelines A (both
raw) and D (both stderiv).

On Pipeline C (raw `.spk` + stderiv `.fetD`/`.pcaD`), the single-flag
check returned `false`, so PCA selected `.pca.N` (wrong basis — the
file doesn't exist, or worse exists with different rank) and feature
reprojection skipped the stderiv transform.  Nudge was silently
writing raw-space projections into `.fetD.N` rows, mixing feature
spaces inside one file.

Split into two flags:

- `spkIsTransformed = m_origSpkPath.contains(".spkD.")` — governs
  the `.spk` write branch (apply `applyStderivTransform` iff on-disk
  `.spk` is in stderiv space, else write raw).
- `fetIsStderiv = m_origFetPath.contains(".fetD.")` — governs PCA
  basis selection (`.pcaD.N` vs `.pca.N`) and whether the raw
  waveform is transformed before projecting onto eigenvectors.

Decision gates after the refactor:

| Site | Flag used | File |
|---|---|---|
| realign `.spk` write | `spkIsTransformed` | klustersdoc.cpp:3230 |
| nudge `.spk` write | `isStderivSpk` (= `spkIsTransformed`) | klustersdoc.cpp:3779 |
| nudge feature reprojection (`makeFetRow`) | `isStderivFet` (= `fetIsStderiv && pca.valid()`) | klustersdoc.cpp:3689, 3712 |
| PCA basis file selection | `fetIsStderiv` | klustersdoc.cpp:2599, 3531 |

Pipeline A and Pipeline D sessions are semantically unchanged (both
flags evaluate identically to the old single flag).  Pipeline C is
now correct.

### Files touched this session

- `src/klustakwik/KlustaKwik.cpp` — banner, build tag
- `src/klustakwik/KK.cpp` — 7 call sites use `pickInputPath`;
  `WritePhase15Checkpoint` preserves D variant through `.pending`
- `src/klusters/src/klusters.cpp` — 5 focus-restoration sites
- `src/klusters/src/klustersdoc.cpp` — `nudge` / `realignSpikes`
  Pipeline-C detection split
- `src/ndmanager-plugins/scripts/ndm_functions` — shared
  `read_kk_param`
- `src/ndmanager-plugins/scripts/ndm_klustakwik` — local
  `read_kk_param` removed; 7 missing param reads added
- `src/ndmanager-plugins/scripts/ndm_subcluster_unmatched` —
  full-parity sandbox KK invocation

</details>

## Earlier changes — KlustaKwik

<details>
<summary>Details</summary>

### nRuns flag (new)

`-nRuns N` sets the number of independent CEM restarts **per chunk** in chunked
mode (`ChunkMinutes > 0`). Each chunk independently runs `CEMTwoPhase` N times
with different random seeds and keeps its best-scoring result before handing off
to Phase 2. Phase 0 preseed, Phase 1.5 realignment, and Phase 2 merge each run
only once per pipeline invocation. This is the per-chunk analogue of `nStarts`
in non-chunked mode.  Setting `nRuns 3`
runs three independent pipeline iterations each seeded differently
(`srand(RandomSeed + run)`).  `MinClusters` and `MaxClusters` revert to their
intended role: per-chunk `TrySplits` bounds only, not an iteration driver.
`nRuns 0` (default) preserves the original loop for backward compatibility.

### Phase 1.5: xcorr alignment + .fil re-extraction (reworked)

Phase 1.5 now runs after all per-chunk `CEMTwoPhase` jobs complete and before
Phase 2 cross-chunk model matching.  It proceeds in two steps:

**Step 1 — `RealignChunkWaveforms`**
For each chunk, builds a per-cluster mean waveform from `.spk`, XcorrDispatch-aligns
each spike against the mean, records the shift in `spikeShifts[]`.
Home-chunk first-write-wins: overlap spikes that appear in both chunk k and k+1 get
their shift from chunk k (the chunk where they naturally live).

**Step 2 — `RefeaturizeFromShifts`**
For each shifted spike, re-extracts the aligned waveform from the `.fil` broadband file
at `(rawTs − shift − PeakSampleIndex)`, selecting only the group's channels via
`GroupChannelIds`.  Projects through the saved PCA eigenvectors (PCAE format,
`.pca.N`), re-normalizes using the per-dimension min/range from `LoadData()`, updates
`Data[]` and corrects the normalized timestamp before Phase 2.
Falls back to circular shift from `.spk` (with wrap-around caveat) when `.fil` is not
available (e.g. after `ndm_stripdat`).

New globals auto-detected from YAML at startup:
- `NbTotalChannels` ← `acquisitionSystem.nChannels`
- `PeakSampleIndex` ← `spikeDetection.channelGroups[g].peakSampleIndex`
- `GroupChannelIds` ← channel list for this electrode group

All three can be overridden on the command line.

### New YAML fields read by KlustaKwikYaml

`KKYamlSpikeParams` now exposes: `nTotalChannels`, `peakSampleIndex`, `channelIds`,
`probeId`, `shankIndex`, `probeFile`, `probeLibraryPath`, `sitePositions`.

### Inline drift estimation (disabled by default)

`_RunInlineDriftEstimation()` is registered but not called unless `probeId` and
`probeFile` are set in the YAML and the `.fil` is present.  Invokes
`process_estimatedrift.py` + `process_applydrift.py` as subprocesses after the final
`.clu` write, producing `SESSION.drift` and `SESSION.chunks.N` for the next run.

### TrySplits — split trial fixes (2026-04-02)

- `CEM(nullptr, 0)` → `CEM(nullptr, 1)` in the split trial: the split candidate
  CEM now runs with `enableSplits=true`, letting `TrySplits` find the best
  multi-cluster solution rather than a single random initialization.
- Split trial `nStartingClusters` raised from 3 → 13 (noise + 12 real), giving
  the split CEM sufficient starting clusters to explore the full structure of each
  candidate cluster.
- Infinite recursion fix: `static thread_local int _trySplitsDepth` counter added.
  Split trials at depth > `SplitRecurseDepth` use `CEM(nullptr, 0)` (no further
  recursion). Safe for OMP parallel regions.
- New `-SplitRecurseDepth N` (default 1): how many levels of recursive splitting
  `TrySplits` explores.  `0` = original behavior; `1` = one level; `2` = two.

### Phase 2.5 SubspaceReclusterPerChunk: 3-phase parallel rewrite

Refactored from a single OMP parallel-for-over-chunks to a three-phase model:

- **Phase A (serial):** For each `(chunk, cluster)` pair, compute cluster mean,
  covariance, top-k eigenvectors, project into whitened k-space, normalize,
  compute null score. Produces a flat `WorkItem` list.
- **Phase B (parallel OMP):** Each work item runs `nRuns` × `startK=2..maxSubK`
  restarts of `CEMTwoPhase` (splits enabled via depth saturation) in a local `KK`
  object. Thread cap = `min(nThreads, nItems)`. `schedule(dynamic)` for load balance.
- **Phase C (serial):** Apply accepted splits to `perChunkClass`/`perChunkModels`.
  Serial because `nextLocalId` is per-chunk shared state.

**Depth saturation:** Before each Phase B `CEMTwoPhase` call,
`_trySplitsDepth = SplitRecurseDepth - SubspaceReclusterDepth` is set, so
`TrySplits` fires once but its split trials are non-recursive.

**ID overflow guard (Phase C):** The packed key `chunkIdx * MaxPossibleClusters +
localClusterId` overflows into adjacent chunk's range if `localClusterId >=
MaxPossibleClusters`. Phase C now counts required new IDs before committing each
split and skips if `nextLocalId + nNewIds >= MaxPossibleClusters`. This was the
root cause of the "random cluster" bug.

New `-SubspaceReclusterDepth N` (default 0): controls `TrySplits` recursion depth
inside SubspaceRecluster sub-CEMs, independently of `-SplitRecurseDepth`.

### MergeChunkModels: iterative cross-chunk passes

The entire chunk-pair loop (votes + Mahal + xcorr) now iterates up to
`-TemplateMatchIters` times, breaking when no new `Union()` calls occur.  Enables
cascade matching: a new xcorr merge in pair (k, k+1) can unlock
previously-unresolved clusters in pair (k+1, k+2).

### Progress banners

Phase 1 OMP loop now emits per-run progress to `stderr` inside a critical section:

```
  [chunk 3/9  run 2/3] score=-1243.6  nclusters=12
```

### Parallelism architecture (2026-04-03)

Phase 1 and Phase 2.5 (`SubspaceReclusterPerChunk`) both flattened to `(chunk×run)`
and `(item×run)` parallel axes respectively. Serial reduction after each selects
best run, restores `Class[]`, runs `MStep`, harvests models. `pointPacked` always
rebuilt downstream from `perChunkClass` unconditionally.

### fet/fetD fallback (1st wave, prior session)

Introduced `pickInputPath` in `KlustaKwik.cpp`: prefers canonical
(`.fet`/`.spk`/`.pca`) and falls back to stderiv D variant
(`.fetD`/`.spkD`/`.pcaD`) when canonical is absent.  Wired into `LoadData` so
reextract-style scripts that produce only the D variant no longer need symlink
shims at the KlustaKwik entry point.

The 2026-04-22 commit completes the propagation to every other session-file open
inside KlustaKwik (see top of this file).

</details>

## Earlier changes — ndmanager-plugins

<details>
<summary>Details</summary>

### process_setupgroups.py

- `parse_probe()` now returns `(shanks, geometries)` — per-site `[x_um, y_um]`
  extracted from `probeFile.sites.geometry` in shank-major order.
- `build_spike_group()` writes `probeId`, `shankIndex`, and `sitePositions_um` onto
  each `spikeDetection.channelGroups` entry so all downstream consumers
  (KlustaKwik, `process_estimatedrift`, `process_localise`) can resolve electrode
  geometry without re-loading the probe file.
- `probes[].id` normalized to `probes[].probeId` on write.

### process_estimatedrift.py

- `read_clu()` handles both binary (`int32` header + `int32` ids, written by
  KlustaKwik and Klusters) and legacy text format.  Previously text-only, causing
  silent failure on binary `.clu` files.
- `n_samp` no longer hardcoded to 52; read from YAML `nSamples` per group via
  `--n-samples-per-group` (comma-separated, one per group).
- `build_group_probe_map()` three-tier resolution: spike group `probeId` first,
  then anatomical group cross-reference, then fallback.
- `build_probe_entry_map()` accepts both `probeId` (canonical) and legacy `id`.
- `get_depths()` reads `sitePositions_um` directly from the YAML group when present,
  returning full-length array with `NaN` for null entries (missing geometry).
- `amplitude_com()` masks `NaN`-depth sites.
- `xcorr_shift()` filters `NaN` from depths before computing inter-site spacing.

### process_localize.py

- `site_positions_from_yaml()` reads inline `sitePositions_um` first; only falls back
  to probe-file lookup when the field is absent or has fewer entries than `n_sites`.

### ndm_estimatedrift

- Builds `--n-samples-per-group` from YAML `nSamples` per group.
- All `xml_read`/`xml_count` → `yaml_read`/`yaml_count`.

### All 15 ndm scripts — YAML migration

`xml_read`/`xml_count` replaced with `yaml_read`/`yaml_count` (69 replacements):
`ndm_concatenate`, `ndm_denoiseuniform`, `ndm_extractspikes`,
`ndm_extractspikes_sdiff`, `ndm_hipass`, `ndm_klustakwik`, `ndm_lfp`,
`ndm_localise`, `ndm_pca`, `ndm_redetectspikes`, `ndm_reorderchannels`,
`ndm_resample`, `ndm_spikegrouper`, `ndm_start`, `ndm_stripdat`.

### ndm_klustakwik (earlier)

- `nRuns` parameter read and passed to KlustaKwik.
- `NbSamplesPerSpike` no longer needs to be passed explicitly (auto-detected from
  YAML), but `nbSamplesPerSpike` is kept in the parameter block for override.

See `doc/design/reextractspikes-v1.md`, `doc/design/reextract-v2.md`,
`doc/design/decomposecollisions.md`, `doc/design/subtractspikes-botm.md`
for the full detail of the recent plugin patch series.

</details>

## Earlier changes — jg05-20120316.yaml (sample session)

<details>
<summary>Details</summary>

- All 13 `spikeDetection.channelGroups` entries annotated with `probeId`,
  `shankIndex`, and `sitePositions_um` (inline electrode site geometry).
- Groups 1–8: Buzsaki64L octrode geometry (0–140 µm depth per shank, 8 sites).
- Groups 9–13: A1x32-6mm-50-177 linear geometry (0–1550 µm, 32 sites total,
  tiled across 5 ndm_spikegrouper subgroups).
- All 9 `anatomicalDescription.channelGroups` annotated with `probeId`/`shankIndex`.
- `probes[].id` → `probes[].probeId` (canonical key).
- `ndm_klustakwik.nbSamplesPerSpike` corrected: 32 → 41 (matches actual `.spk`
  files).
- Probe 1 entry corrected to reference `A1x32-6mm-50-177.probe`.

</details>

## Deep technical references

| Topic | File |
|---|---|
| KlustaKwik canonical-engine history (v1.7 → neurosuite-3 diff) | `src/kiloklustakwik/CHANGES-inherited-from-canonical.md` |
| KiloKlustaKwik-internal changes (DipSplit, time-shift merging) | `src/kiloklustakwik/CHANGES.md` |
| `reextractspikes` + shadow clustering (first spec) | `doc/design/reextractspikes-v1.md` |
| `ndm_reextractspikes{,_stderiv}` — extension handling, symlink shims | `doc/design/reextract-v2.md` |
| `process_decomposecollisions` — collision decomposition bug fixes | `doc/design/decomposecollisions.md` |
| `process_subtractspikes` — including `botm` mode (Proepper 2015) | `doc/design/subtractspikes-botm.md` |
| `neuroscope` — cluster raster / overlay stall fixes | `doc/design/neuroscope-raster.md` |
| `neuroscope` — full subsystem audit | `doc/design/neuroscope-audit.md` |
| `templates/template.yaml` — parameter block refresh | `doc/design/template-yaml.md` |
| Per-probe empirical KK priors (`kk_build_prior` / `kk_resolve_prior`) | `doc/design/kk-prior.md` |
| ndmanager Pipeline tab (editable node graph + YAML-driven `ndm_start` dispatcher) | `doc/design/ndm-start-root.md` |
| Hardware / OS tuning recipe | `doc/design/optimization.md` |
| Modeling comparison (Layer 1/2 vs BOTM) | `doc/design/modeling-l1-vs-botm.md` |
| KK prior operational workflow | `doc/workflows/empirical-priors.md` |
| Input binding system — the shared engine (Klusters) | `src/klusters/docs/INPUT_SYSTEM.md` |
| Input binding system — NeuroScope adoption | `src/neuroscope/docs/INPUT_SYSTEM.md` |
| Documentation system prototype (MkDocs + pandoc) | `doc/site/README.md` |
