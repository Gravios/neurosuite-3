# =============================================================================
# neurosuite-3 — monorepo top-level build
#
# Builds all components in dependency order using add_subdirectory.
# All packages share a single CMake configure/build invocation; no
# inter-package install steps are required between configure and build.
#
# Differences from the ExternalProject superbuild:
#
#   • Single configure + single build invocation — no stamp-file dance
#   • IDE integration works out of the box (all targets visible)
#   • Incremental rebuilds are fully incremental across package boundaries
#   • SYCL for klustakwik requires -DCMAKE_CXX_COMPILER=icpx at the
#     top level (the per-package pre-project() probe cannot override the
#     compiler once the parent project() has run).  All other GPU backends
#     (CUDA, HIP) work identically to the superbuild.
#
# Usage:
#
#   cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
#   cmake --build build [--parallel N]
#   cmake --install build            # or: sudo cmake --install build
#
# Optional variables (pass with -D):
#
#   CMAKE_INSTALL_PREFIX   Install root       (default: /usr/local)
#   CMAKE_BUILD_TYPE       Release|Debug      (default: Release)
#   Qt6_DIR                Path to Qt6Config.cmake if Qt6 is not on PATH
#   USE_CUDA               ON|OFF             (default: ON — auto-detected)
#   USE_HIP                ON|OFF             (default: ON — auto-detected)
#   USE_SYCL               ON|OFF             (default: ON — auto-detected)
#   CMAKE_CUDA_ARCHITECTURES  e.g. "86;89;120"
#
#   NS_SKIP_<N>         Set to ON to skip a component, e.g.:
#                         -DNS_SKIP_KILOKLUSTAKWIK=ON
#                       Valid names (case-insensitive suffix):
#                         NPHYS_DATA  LIBKLUSTERSSHARED
#                         KLUSTERS    NEUROSCOPE   NDMANAGER
#                         NDMANAGER_PLUGINS        KLUSTAKWIK
#
#   NS_INSTALL_DEPS     Set to ON to automatically install system
#                       dependencies before building (apt/Homebrew/vcpkg).
#
#   NS_VCPKG_ROOT       vcpkg root for Windows NS_INSTALL_DEPS
#                       (default: %VCPKG_ROOT% env var, or C:/vcpkg)
#
# SYCL / Intel oneAPI note:
#   The ExternalProject superbuild allowed klustakwik to probe for icpx
#   before its own project() call.  In a monorepo, the top-level project()
#   locks the compiler for all subdirectories.  To build klustakwik with
#   SYCL support, pass:
#       cmake -B build -DCMAKE_CXX_COMPILER=icpx -DCMAKE_C_COMPILER=icx
#   With a standard compiler, klustakwik will still build cleanly with
#   OpenMP and (if found) CUDA/HIP backends — only the SYCL backend is
#   affected by this constraint.
# =============================================================================

cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# CUDA optional language declaration
#
# CMake requires all project languages to be declared in project().  We
# probe for the CUDA toolkit before calling project() so that the CUDA
# language is only enabled when the toolkit is actually present.
# HIP and SYCL are handled inside the packages that need them.
# ---------------------------------------------------------------------------
include(CheckLanguage)

option(USE_CUDA "Enable NVIDIA CUDA GPU backends" ON)
option(USE_HIP  "Enable AMD ROCm/HIP GPU backends" ON)
option(USE_SYCL "Enable Intel oneAPI SYCL GPU backends" ON)

if(USE_CUDA)
    # Help CMake find nvcc when the toolkit lives in a non-default location.
    # check_language(CUDA) searches CMAKE_CUDA_COMPILER (cache), then PATH,
    # then the directories listed here via CMAKE_PREFIX_PATH / hints.
    # Users can override by passing -DCMAKE_CUDA_COMPILER=/path/to/nvcc.
    if(NOT CMAKE_CUDA_COMPILER)
        # find_program searches PATH (unlike check_language which uses hard-coded
        # locations).  Run it first so an nvcc that is on PATH is always found.
        find_program(_nvcc_on_path nvcc)
        if(_nvcc_on_path)
            set(CMAKE_CUDA_COMPILER "${_nvcc_on_path}"
                CACHE FILEPATH "CUDA compiler" FORCE)
        else()
            # Fallback: common fixed installation roots, platform-specific.
            # On Windows nvcc lives under the versioned toolkit directory;
            # on Linux under /usr/local/cuda*; macOS dropped CUDA after 10.2.
            if(WIN32)
                # Collect all versioned toolkit dirs under Program Files
                file(GLOB _cuda_roots
                    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/*/bin/nvcc.exe"
                    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*/bin/nvcc.exe")
                # Also honour env vars set by the CUDA installer
                foreach(_env_root "$ENV{CUDA_PATH}" "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH_V12_0}")
                    if(EXISTS "${_env_root}/bin/nvcc.exe")
                        list(PREPEND _cuda_roots "${_env_root}/bin/nvcc.exe")
                    endif()
                endforeach()
                if(_cuda_roots)
                    # Sort descending so the highest version wins
                    list(SORT _cuda_roots ORDER DESCENDING)
                    list(GET _cuda_roots 0 _first_nvcc)
                    set(CMAKE_CUDA_COMPILER "${_first_nvcc}"
                        CACHE FILEPATH "CUDA compiler" FORCE)
                endif()
            elseif(APPLE)
                # CUDA toolkit support for macOS was dropped after 10.2 (Mojave).
                # Apple Silicon has no CUDA support at all.  Skip silently.
                message(STATUS "neurosuite-3: CUDA not supported on macOS — skipping")
            else()
                # Linux: versioned symlinks under /usr/local/cuda*
                foreach(_cuda_root
                        "$ENV{CUDA_HOME}"
                        "$ENV{CUDA_PATH}"
                        "/usr/local/cuda"
                        "/usr/local/cuda-13.1"
                        "/usr/local/cuda-13"
                        "/usr/local/cuda-12"
                        "/usr/local/cuda-11"
                        "/opt/cuda")
                    if(EXISTS "${_cuda_root}/bin/nvcc")
                        set(CMAKE_CUDA_COMPILER "${_cuda_root}/bin/nvcc"
                            CACHE FILEPATH "CUDA compiler" FORCE)
                        break()
                    endif()
                endforeach()
            endif()
        endif()
        unset(_nvcc_on_path CACHE)
    endif()

    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        set(_cuda_lang CUDA)
        message(STATUS "neurosuite-3: CUDA compiler — ${CMAKE_CUDA_COMPILER}")
    else()
        message(STATUS "neurosuite-3: CUDA toolkit not found — building without CUDA")
        message(STATUS "  To enable CUDA, install the NVIDIA CUDA Toolkit and either:")
        message(STATUS "    a) Add nvcc to PATH before running cmake, or")
        message(STATUS "    b) Pass -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc")
        set(USE_CUDA OFF CACHE BOOL "" FORCE)
        set(_cuda_lang "")
    endif()
else()
    set(_cuda_lang "")
endif()

# ---------------------------------------------------------------------------
# CUDA architecture selection
#
# Must be set BEFORE project() so it is in the cache when CMake initialises
# the CUDA language — any set() after project(LANGUAGES CUDA) is too late to
# override the auto-detected default.
#
# Strategy:
#   "native"  — CMake 3.24+ queries the installed GPU and compiles only for
#               that SM.  Fast builds, optimal code, requires a GPU at
#               configure time.
#   explicit  — Fall back to an explicit list covering Turing through
#               Blackwell when no GPU is present (CI, headless nodes, etc.).
#
# Users can override from the command line:
#   cmake -DCMAKE_CUDA_ARCHITECTURES="89;120" ...
# ---------------------------------------------------------------------------
if(_cuda_lang AND NOT CMAKE_CUDA_ARCHITECTURES)
    # Detect whether the running machine actually has a CUDA-capable GPU so
    # we can use "native".  cmake --find-package cannot do this at pre-project
    # time, but we can inspect nvidia-smi if it is available.
    find_program(_nvidia_smi nvidia-smi)
    if(_nvidia_smi)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
            set(CMAKE_CUDA_ARCHITECTURES "native"
                CACHE STRING "CUDA architectures (native = auto-detect installed GPU)" FORCE)
            message(STATUS "neurosuite-3: CUDA architectures = native (GPU detected via nvidia-smi)")
        else()
            set(CMAKE_CUDA_ARCHITECTURES "75;86;89;100;120"
                CACHE STRING "CUDA architectures (CMake < 3.24, native not supported)" FORCE)
            message(STATUS "neurosuite-3: CUDA architectures = 75;86;89;100;120 (CMake < 3.24)")
        endif()
    else()
        # No GPU at configure time (headless/CI) — compile for the range of
        # Maxwell through Blackwell that the codebase targets.
        set(CMAKE_CUDA_ARCHITECTURES "75;86;89;100;120"
            CACHE STRING "CUDA architectures (explicit list — no GPU detected)" FORCE)
        message(STATUS "neurosuite-3: CUDA architectures = 75;86;89;100;120 (no GPU at configure time)")
    endif()
    unset(_nvidia_smi CACHE)
endif()

project(neurosuite-3 VERSION 3.0.0 LANGUAGES CXX C ${_cuda_lang})

# ---------------------------------------------------------------------------
# Global language standards
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          OFF)

if(_cuda_lang)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

# ---------------------------------------------------------------------------
# Default build type
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Per-component skip flags
# ---------------------------------------------------------------------------
option(NS_SKIP_NPHYS_DATA          "Skip nphys-data"         OFF)
option(NS_SKIP_LIBKLUSTERSSHARED   "Skip libklustersshared"  OFF)
option(NS_SKIP_KLUSTERS            "Skip klusters"           OFF)
option(NS_SKIP_NEUROSCOPE          "Skip neuroscope"         OFF)
option(NS_SKIP_NDMANAGER           "Skip ndmanager"          OFF)
option(NS_SKIP_NDMANAGER_PLUGINS   "Skip ndmanager-plugins"  OFF)
option(NS_SKIP_KILOKLUSTAKWIK       "Skip kiloklustakwik"      OFF)

# ---------------------------------------------------------------------------
# Dependency installation
# ---------------------------------------------------------------------------
option(NS_INSTALL_DEPS
    "Automatically install system dependencies before building (apt/Homebrew/vcpkg)"
    OFF)
set(NS_VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH
    "vcpkg root directory (Windows only, defaults to VCPKG_ROOT env var or C:/vcpkg)")

if(NS_INSTALL_DEPS)

    # ── Linux (apt) ───────────────────────────────────────────────────────
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(_apt apt-get)
        if(NOT _apt)
            message(WARNING "NS_INSTALL_DEPS: apt-get not found — skipping.")
        else()
            message(STATUS "NS_INSTALL_DEPS: installing apt packages…")
            set(_apt_packages
                build-essential cmake ninja-build pkg-config git
                qt6-base-dev qt6-tools-dev libqt6svg6-dev
                libgl-dev libxkbcommon-dev libxcb-cursor0
                libyaml-cpp-dev libxml2-dev libgsl-dev
                libsamplerate0-dev
                libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
                ffmpeg python3
            )
            execute_process(
                COMMAND sudo apt-get install -y ${_apt_packages}
                RESULT_VARIABLE _apt_result
                ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE
            )
            if(NOT _apt_result EQUAL 0)
                message(FATAL_ERROR
                    "NS_INSTALL_DEPS: apt-get install failed (exit ${_apt_result}).\n"
                    "Try: sudo apt-get install -y ${_apt_packages}")
            endif()
        endif()

    # ── macOS (Homebrew) ──────────────────────────────────────────────────
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        find_program(_brew brew PATHS /opt/homebrew/bin /usr/local/bin)
        if(NOT _brew)
            message(STATUS "NS_INSTALL_DEPS: Homebrew not found — bootstrapping…")
            execute_process(
                COMMAND /bin/bash -c
                    "\"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
                RESULT_VARIABLE _brew_bootstrap
                ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE
            )
            if(NOT _brew_bootstrap EQUAL 0)
                message(FATAL_ERROR
                    "NS_INSTALL_DEPS: Homebrew bootstrap failed.\n"
                    "Install manually from https://brew.sh then re-run cmake.")
            endif()
            find_program(_brew brew PATHS /opt/homebrew/bin /usr/local/bin NO_DEFAULT_PATH)
        endif()
        if(NOT _brew)
            message(FATAL_ERROR "NS_INSTALL_DEPS: brew still not found after bootstrap.")
        endif()
        set(_brew_packages cmake ninja pkg-config qt@6 yaml-cpp gsl libomp
                           ffmpeg libsamplerate)
        execute_process(
            COMMAND ${_brew} install ${_brew_packages}
            RESULT_VARIABLE _brew_result
            ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE
        )
        if(NOT _brew_result EQUAL 0)
            message(FATAL_ERROR
                "NS_INSTALL_DEPS: brew install failed (exit ${_brew_result}).")
        endif()
        if(NOT DEFINED Qt6_DIR)
            execute_process(
                COMMAND ${_brew} --prefix qt@6
                OUTPUT_VARIABLE _qt6_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(_qt6_prefix)
                set(Qt6_DIR "${_qt6_prefix}/lib/cmake/Qt6" CACHE PATH
                    "Qt6 cmake directory (set by NS_INSTALL_DEPS)" FORCE)
                message(STATUS "NS_INSTALL_DEPS: Qt6_DIR → ${Qt6_DIR}")
            endif()
        endif()

    # ── Windows (vcpkg) ───────────────────────────────────────────────────
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(NOT NS_VCPKG_ROOT)
            if(DEFINED ENV{VCPKG_ROOT})
                set(NS_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
            else()
                set(NS_VCPKG_ROOT "C:/vcpkg")
            endif()
        endif()
        set(_vcpkg_exe "${NS_VCPKG_ROOT}/vcpkg.exe")
        if(NOT EXISTS "${_vcpkg_exe}")
            message(STATUS "NS_INSTALL_DEPS: bootstrapping vcpkg at ${NS_VCPKG_ROOT}…")
            find_program(_git git)
            if(NOT _git)
                message(FATAL_ERROR
                    "NS_INSTALL_DEPS: git not found — install Git for Windows first:\n"
                    "  https://git-scm.com/download/win")
            endif()
            execute_process(
                COMMAND ${_git} clone https://github.com/microsoft/vcpkg.git "${NS_VCPKG_ROOT}"
                RESULT_VARIABLE _r ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR "NS_INSTALL_DEPS: vcpkg clone failed.")
            endif()
            execute_process(
                COMMAND "${NS_VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics
                RESULT_VARIABLE _r ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR "NS_INSTALL_DEPS: vcpkg bootstrap failed.")
            endif()
        endif()
        set(_vcpkg_triplet "x64-windows")
        execute_process(
            COMMAND "${_vcpkg_exe}" install
                    yaml-cpp libxml2 gsl libsamplerate ffmpeg
                    --triplet ${_vcpkg_triplet}
            RESULT_VARIABLE _r ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR "NS_INSTALL_DEPS: vcpkg install failed (exit ${_r}).")
        endif()
        if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
            set(CMAKE_TOOLCHAIN_FILE
                "${NS_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "vcpkg toolchain (set by NS_INSTALL_DEPS)" FORCE)
        endif()
        message(STATUS "NS_INSTALL_DEPS: NOTE — Qt6 must still be installed via the Qt Online Installer.")
    else()
        message(WARNING "NS_INSTALL_DEPS: unsupported platform '${CMAKE_SYSTEM_NAME}' — skipping.")
    endif()

endif() # NS_INSTALL_DEPS

# ---------------------------------------------------------------------------
# Host-specific optimizations (Zen 4/5 + Blackwell workstation)
# Enables -march=native, -ffast-math, IPO/LTO, and nvcc --threads.
# Pass -DNS_ZEN_OPT=OFF to disable (e.g. when cross-compiling).
# ---------------------------------------------------------------------------
include(cmake/ZenOptimizations.cmake OPTIONAL)


# ---------------------------------------------------------------------------
# Helper: add one subdirectory with skip-flag support
#
#   ns_add_subdir(<n> <rel_source_path>)
#
# Derives NS_SKIP_<UPPER_NAME> from <n> for the skip check.
# The binary directory is always ${CMAKE_BINARY_DIR}/<n>.
# ---------------------------------------------------------------------------
macro(ns_add_subdir _name _rel_src)
    string(TOUPPER "${_name}" _upper)
    string(REPLACE "-" "_" _upper "${_upper}")
    set(_skip_var "NS_SKIP_${_upper}")
    if(${_skip_var})
        message(STATUS "neurosuite-3: skipping ${_name} (${_skip_var}=ON)")
    else()
        add_subdirectory(
            "${CMAKE_CURRENT_SOURCE_DIR}/${_rel_src}"
            "${CMAKE_BINARY_DIR}/${_name}"
        )
    endif()
endmacro()

# ---------------------------------------------------------------------------
# Build graph
#
#   nphys-data  ──────────────────────────────────────────────────────────┐
#   libklustersshared ──┬──> klusters                                     │
#                       ├──> neuroscope                                   │
#                       └──> ndmanager                                    │
#   ndmanager-plugins ────────────────────────────────────────────────────┤
#   kiloklustakwik ───────────────────────────────────────────────────┘
#
# libklustersshared exports a build-tree targets file via export(EXPORT …)
# alongside its install(EXPORT …).  After add_subdirectory() the config
# and targets files exist in the binary sub-directory; we point
# LibKlustersShared_DIR there so that the downstream find_package() calls
# succeed without an install step.
# ---------------------------------------------------------------------------

# 1. MIME types and desktop icons — no compiler required
ns_add_subdir(nphys-data src/nphys-data)

# 2. Shared Qt6 library
ns_add_subdir(libklustersshared src/libklustersshared)

# After add_subdirectory() libklustersshared's install(TARGETS … EXPORT …) has
# run and the LibKlustersSharedTargets export set is populated.  Generate the
# build-tree targets file now so that the downstream find_package() guard
# (if(NOT TARGET KlustersShared::klustersshared)) also has a valid file on disk
# for users who build libklustersshared standalone and then consume it via
# find_package from the build tree.
if(NOT NS_SKIP_LIBKLUSTERSSHARED)
    export(EXPORT LibKlustersSharedTargets
        NAMESPACE KlustersShared::
        FILE "${CMAKE_BINARY_DIR}/libklustersshared/LibKlustersSharedTargets.cmake")
    set(LibKlustersShared_DIR "${CMAKE_BINARY_DIR}/libklustersshared"
        CACHE PATH "Build-tree LibKlustersShared cmake dir (set by monorepo)" FORCE)
endif()

# 3–5. Qt GUI applications — find_package(LibKlustersShared) resolves to build tree
ns_add_subdir(klusters   src/klusters)
ns_add_subdir(neuroscope src/neuroscope)
ns_add_subdir(ndmanager  src/ndmanager)

# 6. Preprocessing pipeline — no Qt, no libklustersshared dependency
ns_add_subdir(ndmanager-plugins src/ndmanager-plugins)

# 7. Standalone spike sorter — KiloKlustaKwik, the shift-probe refeaturization
#    engine (formerly the experimental KlustaKwikExp fork, now the canonical and
#    only sorter). See src/kiloklustakwik/CHANGES.md for algorithmic details.
#    Disable with -DNS_SKIP_KILOKLUSTAKWIK=ON.
ns_add_subdir(kiloklustakwik src/kiloklustakwik)

# ---------------------------------------------------------------------------
# Explicit build-time dependencies for parallel builds (belt-and-suspenders)
#
# In this monorepo the ordering is already guaranteed without this block.
# libklustersshared is add_subdirectory()'d before its consumers (above) and
# defines both the real target `klustersshared` and the alias
# `KlustersShared::klustersshared`.  Each consumer guards its find_package with
# `if(NOT TARGET KlustersShared::klustersshared)`, so in the monorepo the alias
# is already present, find_package is skipped, and the consumer links the
# *in-tree* target.  Linking an in-tree target (even via an alias) gives CMake
# an automatic build-order edge, so each consumer's link step waits for
# klustersshared.so under "cmake --build --parallel N".  All of
# libklustersshared's headers — including the committed libklustersshared_export.h
# — exist at configure time, so consumer AUTOMOC never needs the library
# *built*, only its headers: there is no autogen race to lose.
#
# The edges below are therefore redundant in the normal monorepo build.  They
# are kept as insurance for the cases the alias does NOT cover: a consumer built
# standalone against an installed/build-tree libklustersshared (where
# find_package yields an IMPORTED target, which carries no automatic build-order
# edge), and any future change that has libklustersshared *generate* a header at
# build time that a consumer's autogen would then consume.
#
# Only targets that actually LINK libklustersshared are listed.  kiloklustakwik
# and every ndmanager-plugin merely shared-source the Qt-free xcorr / progressbar
# files (committed sources, no generated artifact) and so have no build-order
# dependency on the klustersshared target; klusters_convert_binary likewise links
# nothing from libklustersshared.  All are intentionally omitted.
# ---------------------------------------------------------------------------
if(NOT NS_SKIP_LIBKLUSTERSSHARED)
    foreach(_consumer klusters neuroscope ndmanager)
        if(TARGET ${_consumer})
            add_dependencies(${_consumer} klustersshared)
        else()
            message(WARNING
                "neurosuite-3: expected consumer target '${_consumer}' not found "
                "when wiring the libklustersshared build-order edge — it may have "
                "been renamed. Ordering is still enforced via the in-tree alias "
                "link, but this redundant safety edge is now absent.")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "neurosuite-3 monorepo configured")
message(STATUS "  Install prefix : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  CUDA           : ${USE_CUDA}")
message(STATUS "  HIP            : ${USE_HIP}")
message(STATUS "  SYCL           : ${USE_SYCL}")
message(STATUS "")
message(STATUS "To build:    cmake --build ${CMAKE_BINARY_DIR}")
message(STATUS "To install:  cmake --install ${CMAKE_BINARY_DIR}")
message(STATUS "")
