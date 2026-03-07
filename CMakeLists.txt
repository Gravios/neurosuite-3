# =============================================================================
# neurosuite-3 — top-level superbuild
#
# Builds all components in dependency order using CMake's ExternalProject
# module.  Every sub-project is configured, built, and installed in sequence;
# the install tree (CMAKE_INSTALL_PREFIX) is also added to CMAKE_PREFIX_PATH
# so that packages that depend on libklustersshared find it automatically.
#
# Usage (identical on Linux, macOS, and Windows):
#
#   cmake -B build -DCMAKE_INSTALL_PREFIX=/usr/local
#   cmake --build build
#
# Optional variables (pass with -D):
#
#   CMAKE_INSTALL_PREFIX   Install root       (default: /usr/local)
#   CMAKE_BUILD_TYPE       Release|Debug      (default: Release)
#   JOBS                   Parallel jobs      (default: cmake --build default)
#   Qt6_DIR                Path to Qt6Config.cmake if Qt6 is not on PATH
#   USE_CUDA               ON|OFF             (default: ON — auto-detected)
#   USE_HIP                ON|OFF             (default: ON — auto-detected)
#   USE_SYCL               ON|OFF             (default: ON — auto-detected)
#   CMAKE_CUDA_ARCHITECTURES  e.g. "86;89;120"
#
#   NS_SKIP_<NAME>         Set to ON to skip a component, e.g.:
#                            -DNS_SKIP_KLUSTAKWIK=ON
#                            #                          Valid names (case-insensitive suffix):
#                            NPHYS_DATA  LIBKLUSTERSSHARED
#                            KLUSTERS    NEUROSCOPE   NDMANAGER
#                            NDMANAGER_PLUGINS        KLUSTAKWIK
#                            SPIKEREALIGN
#
#   NS_INSTALL_DEPS     Set to ON to automatically install system dependencies
#                       before building (default: OFF).
#                         Linux   — sudo apt-get install <packages>
#                         macOS   — brew install <packages>; bootstraps Homebrew
#                                   if absent; also sets Qt6_DIR automatically
#                         Windows — bootstraps vcpkg if absent, then
#                                   vcpkg install <packages>; Qt6 must still be
#                                   installed via the Qt Online Installer.
#
#   NS_VCPKG_ROOT       vcpkg root for Windows NS_INSTALL_DEPS
#                       (default: %VCPKG_ROOT% env var, or C:/vcpkg)
#
# Examples:
#
#   # Default build
#   cmake -B build && cmake --build build
#
#   # Install dependencies then build (Linux)
#   cmake -B build -DNS_INSTALL_DEPS=ON
#   cmake --build build
#
#   # Install dependencies then build (macOS — Qt6_DIR set automatically)
#   cmake -B build -DNS_INSTALL_DEPS=ON -DCMAKE_INSTALL_PREFIX="$HOME/.local"
#   cmake --build build
#
#   # Install dependencies then build (Windows, from VS Developer Prompt)
#   cmake -B build -DNS_INSTALL_DEPS=ON ^
#         -DQt6_DIR=C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
#   cmake --build build
#
#   # CPU-only, custom prefix
#   cmake -B build -DCMAKE_INSTALL_PREFIX=~/.local \
#         -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF
#   cmake --build build
#
#   # Blackwell GPU, explicit arch
#   cmake -B build -DCMAKE_CUDA_ARCHITECTURES="89;120"
#   cmake --build build
#
#   # Skip heavy optional components
#   cmake -B build -DNS_SKIP_KLUSTAKWIK=ON #   cmake --build build
#
#   # Qt6 installed to a non-standard location
#   cmake -B build -DQt6_DIR=/opt/Qt/6.7.0/gcc_64/lib/cmake/Qt6
#   cmake --build build
# =============================================================================

cmake_minimum_required(VERSION 3.25)
project(neurosuite-3 VERSION 3.0.0 LANGUAGES NONE)

include(ExternalProject)

# ── Options ──────────────────────────────────────────────────────────────────

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(JOBS "" CACHE STRING "Parallel jobs for sub-builds (empty = CMake default)")

# GPU backends — forwarded to every sub-project that accepts them
option(USE_CUDA "Enable NVIDIA CUDA GPU backends" ON)
option(USE_HIP  "Enable AMD ROCm/HIP GPU backends" ON)
option(USE_SYCL "Enable Intel oneAPI SYCL GPU backends" ON)

# Per-component skip flags
option(NS_SKIP_NPHYS_DATA          "Skip nphys-data"         OFF)
option(NS_SKIP_LIBKLUSTERSSHARED   "Skip libklustersshared"  OFF)
option(NS_SKIP_KLUSTERS            "Skip klusters"           OFF)
option(NS_SKIP_NEUROSCOPE          "Skip neuroscope"         OFF)
option(NS_SKIP_NDMANAGER           "Skip ndmanager"          OFF)
option(NS_SKIP_NDMANAGER_PLUGINS   "Skip ndmanager-plugins"  OFF)
option(NS_SKIP_KLUSTAKWIK          "Skip klustakwik"         OFF)

# Dependency installation
option(NS_INSTALL_DEPS
    "Automatically install system dependencies before building (apt / Homebrew / vcpkg)"
    OFF)
set(NS_VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH
    "vcpkg root directory (Windows only, defaults to VCPKG_ROOT env var or C:/vcpkg)")

# ── Dependency installation ────────────────────────────────────────────────────
#
# Runs at configure time so that find_package() calls in the sub-projects
# succeed on the same cmake invocation.  Only runs when NS_INSTALL_DEPS=ON.
# GPU packages (CUDA, ROCm, oneAPI) are intentionally excluded — those
# require vendor-specific installers and are documented in doc/<component>/.
#
# Linux  : sudo apt-get install
# macOS  : brew install  (Homebrew bootstrapped automatically if absent)
# Windows: vcpkg install (vcpkg root resolved from NS_VCPKG_ROOT / VCPKG_ROOT)

if(NS_INSTALL_DEPS)

    # ── Linux (apt) ───────────────────────────────────────────────────────────
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(_apt apt-get)
        if(NOT _apt)
            message(WARNING "NS_INSTALL_DEPS: apt-get not found — skipping dependency installation.")
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
                ECHO_OUTPUT_VARIABLE
                ECHO_ERROR_VARIABLE
            )
            if(NOT _apt_result EQUAL 0)
                message(FATAL_ERROR
                    "NS_INSTALL_DEPS: apt-get install failed (exit ${_apt_result}).\n"
                    "Try running manually:\n"
                    "  sudo apt-get install -y ${_apt_packages}")
            endif()
            message(STATUS "NS_INSTALL_DEPS: apt packages installed.")
        endif()

    # ── macOS (Homebrew) ──────────────────────────────────────────────────────
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        find_program(_brew brew)
        if(NOT _brew)
            message(STATUS "NS_INSTALL_DEPS: Homebrew not found — bootstrapping…")
            execute_process(
                COMMAND /bin/bash -c
                    "\"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
                RESULT_VARIABLE _brew_bootstrap
                ECHO_OUTPUT_VARIABLE
                ECHO_ERROR_VARIABLE
            )
            if(NOT _brew_bootstrap EQUAL 0)
                message(FATAL_ERROR
                    "NS_INSTALL_DEPS: Homebrew bootstrap failed.\n"
                    "Install manually from https://brew.sh then re-run cmake.")
            endif()
            # Re-detect after bootstrap (arm64 vs x86_64 prefix differs)
            find_program(_brew brew
                PATHS /opt/homebrew/bin /usr/local/bin
                NO_DEFAULT_PATH)
        endif()
        if(NOT _brew)
            message(FATAL_ERROR "NS_INSTALL_DEPS: brew still not found after bootstrap.")
        endif()

        message(STATUS "NS_INSTALL_DEPS: installing Homebrew packages…")
        set(_brew_packages cmake ninja pkg-config qt@6 yaml-cpp gsl libomp
                           ffmpeg libsamplerate)
        execute_process(
            COMMAND ${_brew} install ${_brew_packages}
            RESULT_VARIABLE _brew_result
            ECHO_OUTPUT_VARIABLE
            ECHO_ERROR_VARIABLE
        )
        if(NOT _brew_result EQUAL 0)
            message(FATAL_ERROR
                "NS_INSTALL_DEPS: brew install failed (exit ${_brew_result}).\n"
                "Try running manually:\n"
                "  brew install ${_brew_packages}")
        endif()

        # Tell CMake where Homebrew's Qt6 lives so sub-projects find it.
        if(NOT DEFINED Qt6_DIR)
            execute_process(
                COMMAND ${_brew} --prefix qt@6
                OUTPUT_VARIABLE _qt6_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(_qt6_prefix)
                set(Qt6_DIR "${_qt6_prefix}/lib/cmake/Qt6" CACHE PATH
                    "Qt6 cmake directory (set by NS_INSTALL_DEPS)" FORCE)
                message(STATUS "NS_INSTALL_DEPS: Qt6_DIR set to ${Qt6_DIR}")
            endif()
        endif()
        message(STATUS "NS_INSTALL_DEPS: Homebrew packages installed.")

    # ── Windows (vcpkg) ───────────────────────────────────────────────────────
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # Resolve vcpkg root: option > env var > default location
        if(NOT NS_VCPKG_ROOT)
            if(DEFINED ENV{VCPKG_ROOT})
                set(NS_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
            else()
                set(NS_VCPKG_ROOT "C:/vcpkg")
            endif()
        endif()

        set(_vcpkg_exe "${NS_VCPKG_ROOT}/vcpkg.exe")
        if(NOT EXISTS "${_vcpkg_exe}")
            message(STATUS "NS_INSTALL_DEPS: vcpkg not found at ${NS_VCPKG_ROOT} — bootstrapping…")
            find_program(_git git)
            if(NOT _git)
                message(FATAL_ERROR
                    "NS_INSTALL_DEPS: git not found — install Git for Windows first:\n"
                    "  https://git-scm.com/download/win")
            endif()
            execute_process(
                COMMAND ${_git} clone https://github.com/microsoft/vcpkg.git
                        "${NS_VCPKG_ROOT}"
                RESULT_VARIABLE _vcpkg_clone
                ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE
            )
            if(NOT _vcpkg_clone EQUAL 0)
                message(FATAL_ERROR "NS_INSTALL_DEPS: vcpkg clone failed.")
            endif()
            execute_process(
                COMMAND "${NS_VCPKG_ROOT}/bootstrap-vcpkg.bat" -disableMetrics
                RESULT_VARIABLE _vcpkg_boot
                ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE
            )
            if(NOT _vcpkg_boot EQUAL 0)
                message(FATAL_ERROR "NS_INSTALL_DEPS: vcpkg bootstrap failed.")
            endif()
        endif()

        message(STATUS "NS_INSTALL_DEPS: installing vcpkg packages…")
        set(_vcpkg_triplet "x64-windows")
        set(_vcpkg_packages
            yaml-cpp libxml2 gsl libsamplerate ffmpeg
        )
        execute_process(
            COMMAND "${_vcpkg_exe}" install ${_vcpkg_packages}
                    --triplet ${_vcpkg_triplet}
            RESULT_VARIABLE _vcpkg_result
            ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE
        )
        if(NOT _vcpkg_result EQUAL 0)
            message(FATAL_ERROR
                "NS_INSTALL_DEPS: vcpkg install failed (exit ${_vcpkg_result}).\n"
                "Try running manually from a VS Developer Prompt:\n"
                "  ${_vcpkg_exe} install ${_vcpkg_packages} --triplet ${_vcpkg_triplet}")
        endif()

        # Inject the vcpkg toolchain into the sub-project common args so that
        # find_package() in each sub-project resolves the vcpkg-installed libs.
        set(_vcpkg_toolchain
            "${NS_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        list(APPEND _common_args
            "-DCMAKE_TOOLCHAIN_FILE=${_vcpkg_toolchain}"
            "-DVCPKG_TARGET_TRIPLET=${_vcpkg_triplet}")
        message(STATUS "NS_INSTALL_DEPS: vcpkg packages installed.")
        message(STATUS "NS_INSTALL_DEPS: NOTE — Qt6 must be installed separately via the Qt Online Installer.")
        message(STATUS "NS_INSTALL_DEPS: Set -DQt6_DIR=C:\\Qt\\6.x.x\\msvc2022_64\\lib\\cmake\\Qt6")

    else()
        message(WARNING
            "NS_INSTALL_DEPS: unsupported platform '${CMAKE_SYSTEM_NAME}' — skipping.")
    endif()

endif() # NS_INSTALL_DEPS

# ── Derived paths ─────────────────────────────────────────────────────────────

set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_pfx "${CMAKE_INSTALL_PREFIX}")

# CMAKE_PREFIX_PATH needs to include the install prefix so that
# find_package(LibKlustersShared) works when configuring dependent packages.
list(APPEND CMAKE_PREFIX_PATH "${_pfx}")

# ── Sudo detection ────────────────────────────────────────────────────────────
#
# Walk up from the install prefix to find the first existing ancestor, then
# test whether the current user can write to it.  If not, prepend sudo to
# every cmake --install call so that sub-projects can install to system paths
# (e.g. /usr/local) without requiring the entire cmake --build invocation to
# be run as root.

if(UNIX)
    set(_test_dir "${_pfx}")
    while(NOT EXISTS "${_test_dir}")
        get_filename_component(_test_dir "${_test_dir}" DIRECTORY)
        if("${_test_dir}" STREQUAL "/")
            break()
        endif()
    endwhile()

    execute_process(
        COMMAND test -w "${_test_dir}"
        RESULT_VARIABLE _pfx_not_writable
    )
    if(_pfx_not_writable)
        find_program(_sudo_exe sudo)
        if(_sudo_exe)
            set(_install_prefix "${_sudo_exe}" "${CMAKE_COMMAND}")
            message(STATUS
                "Install prefix ${_pfx} not writable by current user — "
                "install steps will use sudo.")
        else()
            set(_install_prefix "${CMAKE_COMMAND}")
            message(WARNING
                "Install prefix ${_pfx} not writable and sudo not found. "
                "Install steps may fail. Run cmake --build as root or "
                "choose a writable prefix with -DCMAKE_INSTALL_PREFIX=.")
        endif()
    else()
        set(_install_prefix "${CMAKE_COMMAND}")
    endif()
else()
    # Windows: no sudo concept, cmake --install runs as the current user
    set(_install_prefix "${CMAKE_COMMAND}")
endif()

# ── Job count flag ────────────────────────────────────────────────────────────

if(JOBS)
    set(_parallel "--parallel;${JOBS}")
else()
    set(_parallel "")
endif()

# ── Common cmake args forwarded to every sub-project ─────────────────────────

set(_common_args
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX=${_pfx}
    -DCMAKE_PREFIX_PATH=${_pfx}
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

# Forward Qt6_DIR if the user supplied it
if(DEFINED Qt6_DIR)
    list(APPEND _common_args -DQt6_DIR=${Qt6_DIR})
endif()

# Forward CUDA arch override if supplied
if(DEFINED CMAKE_CUDA_ARCHITECTURES)
    list(APPEND _common_args -DCMAKE_CUDA_ARCHITECTURES=${CMAKE_CUDA_ARCHITECTURES})
endif()

# ── GPU args — only forwarded to packages that use them ───────────────────────

set(_gpu_args
    -DUSE_CUDA=${USE_CUDA}
    -DUSE_HIP=${USE_HIP}
    -DUSE_SYCL=${USE_SYCL}
)

# ── Helper: add one ExternalProject with standard boilerplate ─────────────────
#
#  ns_add_package(<name> <source_subdir>
#                 [DEPENDS <dep1> ...]
#                 [EXTRA_ARGS <-DFOO=bar> ...])
#
macro(ns_add_package _name _subdir)
    cmake_parse_arguments(_p "" "" "DEPENDS;EXTRA_ARGS" ${ARGN})

    # Compute the skip-flag variable name: convert to uppercase, replace - with _
    string(TOUPPER "${_name}" _upper)
    string(REPLACE "-" "_" _upper "${_upper}")
    set(_skip_var "NS_SKIP_${_upper}")

    if(${_skip_var})
        message(STATUS "neurosuite-3: skipping ${_name} (${_skip_var}=ON)")
    else()
        ExternalProject_Add(${_name}
            SOURCE_DIR      "${_src}/${_subdir}"
            BINARY_DIR      "${CMAKE_BINARY_DIR}/${_name}"
            INSTALL_DIR     "${_pfx}"
            CMAKE_ARGS      ${_common_args} ${_p_EXTRA_ARGS}
            BUILD_COMMAND   ${CMAKE_COMMAND} --build <BINARY_DIR> ${_parallel}
            INSTALL_COMMAND ${_install_prefix} --install <BINARY_DIR>
            DEPENDS         ${_p_DEPENDS}
            # Keep the stamp/log files inside the binary dir to avoid polluting src/
            STAMP_DIR       "${CMAKE_BINARY_DIR}/${_name}/stamp"
            LOG_CONFIGURE   ON
            LOG_BUILD       ON
            LOG_INSTALL     ON
            LOG_OUTPUT_ON_FAILURE ON
        )
    endif()
endmacro()

# ── Build graph ───────────────────────────────────────────────────────────────
#
#  nphys-data  ──────────────────────────────────────┐
#  libklustersshared ──┬──> klusters                 │
#                      ├──> neuroscope               │ (all independent
#                      └──> ndmanager                │  of each other)
#  ndmanager-plugins ────────────────────────────────┤
#  klustakwik ───────────────────────────────────────┤
#
# libklustersshared must be *installed* before klusters/neuroscope/ndmanager
# are *configured* (they call find_package(LibKlustersShared)).  The DEPENDS
# clause on each of those packages ensures ExternalProject serialises them
# correctly: configure → build → install of libklustersshared runs fully
# before configure of the dependents begins.

# 1. MIME types and desktop icons — no compiler required
ns_add_package(nphys-data nphys-data)

# 2. Shared Qt6 library — all GUI packages depend on this
ns_add_package(libklustersshared libklustersshared
    DEPENDS nphys-data)

# 3–5. Qt GUI applications — depend on libklustersshared being installed first
ns_add_package(klusters klusters
    DEPENDS libklustersshared
    EXTRA_ARGS ${_gpu_args})

ns_add_package(neuroscope neuroscope
    DEPENDS libklustersshared)

ns_add_package(ndmanager ndmanager
    DEPENDS libklustersshared)

# 6. Preprocessing pipeline — no Qt, no libklustersshared dependency
ns_add_package(ndmanager-plugins ndmanager-plugins
    EXTRA_ARGS ${_gpu_args})

# 7. Standalone spike sorter
ns_add_package(klustakwik klustakwik
    EXTRA_ARGS ${_gpu_args})

# ── Summary ───────────────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "neurosuite-3 superbuild configured")
message(STATUS "  Install prefix : ${_pfx}")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install deps   : ${NS_INSTALL_DEPS}")
message(STATUS "  CUDA           : ${USE_CUDA}")
message(STATUS "  HIP            : ${USE_HIP}")
message(STATUS "  SYCL           : ${USE_SYCL}")
if(JOBS)
    message(STATUS "  Jobs           : ${JOBS}")
endif()
message(STATUS "")
message(STATUS "To build and install:")
message(STATUS "  cmake --build ${CMAKE_BINARY_DIR}")
message(STATUS "")
