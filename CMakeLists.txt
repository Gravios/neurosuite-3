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
#                         -DNS_SKIP_KLUSTAKWIK=ON
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

cmake_minimum_required(VERSION 3.25)

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
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        set(_cuda_lang CUDA)
    else()
        message(STATUS "neurosuite-3: CUDA toolkit not found — building without CUDA")
        set(USE_CUDA OFF CACHE BOOL "" FORCE)
        set(_cuda_lang "")
    endif()
else()
    set(_cuda_lang "")
endif()

project(neurosuite-3 VERSION 3.0.0 LANGUAGES CXX C ${_cuda_lang})

# ---------------------------------------------------------------------------
# Global language standards
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
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
option(NS_SKIP_KLUSTAKWIK          "Skip klustakwik"         OFF)

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
#   klustakwik ───────────────────────────────────────────────────────────┘
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

# After add_subdirectory() both LibKlustersSharedConfig.cmake and
# LibKlustersSharedTargets.cmake exist in ${CMAKE_BINARY_DIR}/libklustersshared.
# Advertise that path so the downstream find_package(LibKlustersShared) calls
# succeed without an install step.
if(NOT NS_SKIP_LIBKLUSTERSSHARED)
    set(LibKlustersShared_DIR "${CMAKE_BINARY_DIR}/libklustersshared"
        CACHE PATH "Build-tree LibKlustersShared cmake dir (set by monorepo)" FORCE)
endif()

# 3–5. Qt GUI applications — find_package(LibKlustersShared) resolves to build tree
ns_add_subdir(klusters   src/klusters)
ns_add_subdir(neuroscope src/neuroscope)
ns_add_subdir(ndmanager  src/ndmanager)

# 6. Preprocessing pipeline — no Qt, no libklustersshared dependency
ns_add_subdir(ndmanager-plugins src/ndmanager-plugins)

# 7. Standalone spike sorter
ns_add_subdir(klustakwik src/klustakwik)

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
