#!/usr/bin/env bash
# =============================================================================
# build-neurosuite-macos.sh
# Build and install all NeuroSuite-3 packages on macOS (Apple Clang + CMake).
#
# Build order (dependency graph):
#   1. nphys-data          -- data files only, no compiler needed
#   2. libklustersshared   -- shared Qt6 dylib; all Qt apps depend on it
#   3. klusters            -- depends on libklustersshared
#   4. neuroscope          -- depends on libklustersshared
#   5. ndmanager           -- depends on libklustersshared
#   6. ndmanager-plugins   -- standalone C/C++ (no Qt); optional FFmpeg/OpenMP
#   7. klustakwik          -- standalone C/C++; optional OpenMP (no GPU on macOS)
#   8. spikerealign        -- standalone C/C++; optional OpenMP (no GPU on macOS)
#
# GPU BACKENDS (CUDA / HIP / SYCL)
#   All GPU backends are disabled by default on macOS:
#     - CUDA: Apple dropped NVIDIA GPU support in macOS 10.14
#     - HIP/ROCm: not supported on macOS
#     - SYCL/oneAPI: not officially supported on macOS
#   Pass -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF (done automatically).
#
# OPENMP ON MACOS
#   Apple Clang does not ship with OpenMP. This script installs libomp via
#   Homebrew and passes the required CMake hint flags so that find_package(OpenMP)
#   succeeds. Use --no-openmp to skip this and build without OpenMP support.
#
# REQUIREMENTS
#   - macOS 13 (Ventura) or later recommended
#   - Xcode Command Line Tools:  xcode-select --install
#   - Homebrew (https://brew.sh) — installed automatically if absent
#   - CMake 3.21+  (installed via Homebrew automatically)
#   - Qt 6.6+      (installed via Homebrew automatically, or use --qt-dir)
#
# USAGE
#   ./build-neurosuite-macos.sh [OPTIONS]
#
# OPTIONS
#   --prefix     DIR   Install destination      (default: $HOME/.local)
#   --build-dir  DIR   Build tree parent        (default: ./build)
#   --source-dir DIR   src/ parent directory    (default: current directory)
#   --jobs       N     Parallel build jobs      (default: sysctl -n hw.ncpu)
#   --qt-dir     DIR   Qt6_DIR cmake path       (default: auto-detected)
#   --brew       DIR   Homebrew prefix override (default: auto-detected)
#   --no-install       Build but do not install
#   --no-brew          Skip Homebrew dependency installation
#   --no-openmp        Skip libomp install; build without OpenMP
#   --with-ffmpeg      brew install ffmpeg (needed by process_extractleds)
#   --with-libsamplerate  brew install libsamplerate (recommended; avoids
#                         vendored fallback that needs autoconf on macOS)
#   --skip       PKG   Skip a package (repeat for multiple). Valid names:
#                      nphys-data libklustersshared klusters neuroscope
#                      ndmanager ndmanager-plugins klustakwik spikerealign
#   --clean            Remove each package's build tree after install
#   --clean-on-fail    Remove build tree only when a package fails
#   -h, --help         Show this help
#
# EXAMPLES
#   ./build-neurosuite-macos.sh
#   ./build-neurosuite-macos.sh --prefix /opt/neurosuite --with-ffmpeg --with-libsamplerate
#   ./build-neurosuite-macos.sh --qt-dir /opt/homebrew/opt/qt/lib/cmake/Qt6
#   ./build-neurosuite-macos.sh --skip klustakwik --skip spikerealign
#   ./build-neurosuite-macos.sh --no-brew --no-openmp
# =============================================================================

set -euo pipefail

# ── Colours ───────────────────────────────────────────────────────────────────
if [[ -t 1 ]]; then
    C_RESET='\033[0m'
    C_BOLD='\033[1m'
    C_GREEN='\033[1;32m'
    C_YELLOW='\033[1;33m'
    C_CYAN='\033[1;36m'
    C_RED='\033[1;31m'
else
    C_RESET='' C_BOLD='' C_GREEN='' C_YELLOW='' C_CYAN='' C_RED=''
fi

log()     { echo -e "${C_CYAN}[build]${C_RESET} $*"; }
success() { echo -e "${C_GREEN}[  OK ]${C_RESET} $*"; }
warn()    { echo -e "${C_YELLOW}[ WARN]${C_RESET} $*"; }
error()   { echo -e "${C_RED}[FAIL ]${C_RESET} $*" >&2; }
header()  { echo -e "\n${C_BOLD}══════════════════════════════════════════════${C_RESET}"
            echo -e "${C_BOLD}  $*${C_RESET}"
            echo -e "${C_BOLD}══════════════════════════════════════════════${C_RESET}"; }

# ── Defaults ──────────────────────────────────────────────────────────────────
PREFIX="${HOME}/.local"
BUILD_BASE="$(pwd)/build"
SOURCE_BASE="$(pwd)"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
QT_DIR=""
BREW_PREFIX=""           # auto-detected below
DO_INSTALL=true
DO_BREW=true
DO_OPENMP=true
WITH_FFMPEG=false
WITH_LIBSAMPLERATE=false
CLEAN=false
CLEAN_ON_FAIL=false
SKIP_PKGS=()

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)              PREFIX="$2";              shift 2 ;;
        --build-dir)           BUILD_BASE="$2";          shift 2 ;;
        --source-dir)          SOURCE_BASE="$2";         shift 2 ;;
        --jobs)                JOBS="$2";                shift 2 ;;
        --qt-dir)              QT_DIR="$2";              shift 2 ;;
        --brew)                BREW_PREFIX="$2";         shift 2 ;;
        --no-install)          DO_INSTALL=false;         shift   ;;
        --no-brew)             DO_BREW=false;            shift   ;;
        --no-openmp)           DO_OPENMP=false;          shift   ;;
        --with-ffmpeg)         WITH_FFMPEG=true;         shift   ;;
        --with-libsamplerate)  WITH_LIBSAMPLERATE=true;  shift   ;;
        --clean)               CLEAN=true;               shift   ;;
        --clean-on-fail)       CLEAN_ON_FAIL=true;       shift   ;;
        --skip)                SKIP_PKGS+=("$2");        shift 2 ;;
        -h|--help)
            sed -n '3,67p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) error "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Helper: check if a package is in the skip list ────────────────────────────
should_skip() {
    local pkg="$1"
    for s in "${SKIP_PKGS[@]+"${SKIP_PKGS[@]}"}"; do
        [[ "$s" == "$pkg" ]] && return 0
    done
    return 1
}

# ── Helper: remove a build directory ─────────────────────────────────────────
remove_build() {
    local build="$1" label="$2"
    [[ -d "$build" ]] || return 0
    log "Removing build tree: $build"
    if [[ -w "$build" ]] || [[ -w "$(dirname "$build")" ]]; then
        rm -rf "$build"
    else
        sudo rm -rf "$build"
    fi
    log "Build tree removed for ${label}."
}

# ── Helper: build + (optionally) install one CMake package ───────────────────
cmake_build() {
    local label="$1" src="$2"
    local build="${BUILD_BASE}/${label}"
    shift 2
    # Remaining args are extra -D flags forwarded to cmake configure.

    if should_skip "$label"; then
        warn "Skipping ${label} (--skip requested)"
        return 0
    fi

    header "Building ${label}"
    log "Source : ${src}"
    log "Build  : ${build}"
    log "Prefix : ${PREFIX}"

    if [[ ! -f "${src}/CMakeLists.txt" ]]; then
        error "Source not found for ${label}: ${src}"
        exit 1
    fi

    mkdir -p "${build}"

    _do_build_and_install() {
        log "Configuring…"
        cmake -S "${src}" -B "${build}" \
            -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_PREFIX_PATH="${PREFIX}" \
            -DCMAKE_MACOSX_RPATH=ON \
            -DCMAKE_INSTALL_RPATH="${RPATH_LIST}" \
            -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
            "$@"

        log "Compiling (jobs: ${JOBS})…"
        cmake --build "${build}" --parallel "${JOBS}"

        if ${DO_INSTALL}; then
            log "Installing…"
            if [[ -w "${PREFIX}" ]] || { [[ -w "$(dirname "${PREFIX}")" ]] && [[ ! -e "${PREFIX}" ]]; }; then
                cmake --install "${build}"
            else
                warn "Prefix ${PREFIX} not user-writable — using sudo for install"
                sudo cmake --install "${build}"
            fi
            success "${label} installed."
        else
            success "${label} built (install skipped)."
        fi
    }

    if ${CLEAN_ON_FAIL}; then
        if ! ( _do_build_and_install "$@" ); then
            error "${label} failed."
            remove_build "${build}" "${label}"
            exit 1
        fi
    else
        _do_build_and_install "$@"
    fi

    if ${CLEAN} && ${DO_INSTALL}; then
        remove_build "${build}" "${label}"
    fi
}

# =============================================================================
# 0. Pre-flight: Homebrew + toolchain
# =============================================================================
header "NeuroSuite-3 macOS Build Script"

# Detect Homebrew prefix (Apple Silicon: /opt/homebrew; Intel: /usr/local)
if [[ -z "$BREW_PREFIX" ]]; then
    if [[ -x /opt/homebrew/bin/brew ]]; then
        BREW_PREFIX=/opt/homebrew
    elif [[ -x /usr/local/bin/brew ]]; then
        BREW_PREFIX=/usr/local
    elif command -v brew &>/dev/null; then
        BREW_PREFIX="$(brew --prefix)"
    else
        BREW_PREFIX=""
    fi
fi

log "macOS version  : $(sw_vers -productVersion 2>/dev/null || echo unknown)"
log "Architecture   : $(uname -m)"
log "Homebrew prefix: ${BREW_PREFIX:-<not found>}"
log "Source dir     : ${SOURCE_BASE}"
log "Build dir      : ${BUILD_BASE}"
log "Install prefix : ${PREFIX}"
log "Jobs           : ${JOBS}"
log "Install        : ${DO_INSTALL}"
log "Clean          : ${CLEAN} (on-fail: ${CLEAN_ON_FAIL})"
[[ ${#SKIP_PKGS[@]} -gt 0 ]] && log "Skipping       : ${SKIP_PKGS[*]}"

# ── Xcode Command Line Tools ──────────────────────────────────────────────────
if ! xcode-select -p &>/dev/null; then
    error "Xcode Command Line Tools not installed."
    error "Run:  xcode-select --install"
    error "Then re-run this script."
    exit 1
fi
success "Xcode Command Line Tools found: $(xcode-select -p)"

# ── CMake ─────────────────────────────────────────────────────────────────────
if ! command -v cmake &>/dev/null; then
    if ${DO_BREW}; then
        warn "cmake not found — installing via Homebrew…"
        brew install cmake
    else
        error "cmake not found and --no-brew was passed."
        error "Install cmake manually: https://cmake.org/download/"
        exit 1
    fi
fi
CMAKE_VERSION=$(cmake --version | head -1)
success "CMake found: ${CMAKE_VERSION}"

# =============================================================================
# 1. Homebrew dependencies
# =============================================================================
if ${DO_BREW}; then
    header "Installing Homebrew dependencies"

    if [[ -z "$BREW_PREFIX" ]]; then
        warn "Homebrew not found — installing it now…"
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        # Re-detect after install
        if [[ -x /opt/homebrew/bin/brew ]]; then
            BREW_PREFIX=/opt/homebrew
        else
            BREW_PREFIX=/usr/local
        fi
        # Add brew to PATH for the rest of this session
        eval "$("${BREW_PREFIX}/bin/brew" shellenv)"
    fi

    log "brew update…"
    brew update --quiet

    # ── Required ──────────────────────────────────────────────────────────────
    log "Installing required packages…"
    brew install --quiet cmake qt yaml-cpp libxml2 gsl pkg-config

    # ── OpenMP via libomp ─────────────────────────────────────────────────────
    # Apple Clang does not bundle OpenMP; libomp provides it.
    if ${DO_OPENMP}; then
        brew install --quiet libomp
        success "libomp installed."
    fi

    # ── Optional: libsamplerate ───────────────────────────────────────────────
    # process_resample has a vendored libsamplerate-0.1.8 fallback, but it
    # requires autoconf and may not build cleanly on all macOS setups.
    # Using the Homebrew package is strongly recommended.
    if ${WITH_LIBSAMPLERATE}; then
        brew install --quiet libsamplerate
        success "libsamplerate installed."
    else
        warn "--with-libsamplerate not passed. process_resample will try the"
        warn "vendored libsamplerate-0.1.8 (needs autoconf; may fail). Re-run"
        warn "with --with-libsamplerate or --skip ndmanager-plugins to suppress."
    fi

    # ── Optional: FFmpeg ──────────────────────────────────────────────────────
    # Only required by process_extractleds. All other plugins build without it.
    if ${WITH_FFMPEG}; then
        brew install --quiet ffmpeg
        success "ffmpeg installed."
    else
        warn "--with-ffmpeg not passed. process_extractleds (LED tracking) will"
        warn "be skipped by CMake or will fail at configure time. Re-run with"
        warn "--with-ffmpeg if you need LED video extraction."
    fi

    success "Homebrew dependencies installed."
fi

# =============================================================================
# 2. Resolve paths derived from Homebrew
# =============================================================================

# Qt6_DIR — auto-detect from Homebrew qt formula if not specified
if [[ -z "$QT_DIR" ]]; then
    for _qt_candidate in \
        "${BREW_PREFIX}/opt/qt/lib/cmake/Qt6" \
        "${BREW_PREFIX}/opt/qt6/lib/cmake/Qt6" \
        "${BREW_PREFIX}/lib/cmake/Qt6"
    do
        if [[ -f "${_qt_candidate}/Qt6Config.cmake" ]]; then
            QT_DIR="${_qt_candidate}"
            break
        fi
    done
    # Also check the Qt Online Installer locations
    if [[ -z "$QT_DIR" ]]; then
        for _qt_base in /opt/Qt /Applications/Qt ~/Qt; do
            if [[ -d "$_qt_base" ]]; then
                while IFS= read -r -d '' _cfg; do
                    QT_DIR="$(dirname "$_cfg")"
                    break
                done < <(find "$_qt_base" -name Qt6Config.cmake -print0 2>/dev/null | sort -rz)
                [[ -n "$QT_DIR" ]] && break
            fi
        done
    fi
fi

if [[ -z "$QT_DIR" ]]; then
    warn "Qt6 not auto-detected. Qt-dependent packages may fail."
    warn "Pass --qt-dir /path/to/Qt/6.x.y/macos/lib/cmake/Qt6"
else
    success "Qt6_DIR: ${QT_DIR}"
    # Derive Qt lib dir (two levels up from .../lib/cmake/Qt6 → .../lib)
    QT_LIB_DIR="$(cd "${QT_DIR}/../.." && pwd)"
fi

# libomp paths
LIBOMP_PREFIX=""
LIBOMP_INC=""
LIBOMP_LIB=""
if ${DO_OPENMP} && [[ -n "$BREW_PREFIX" ]]; then
    LIBOMP_PREFIX="${BREW_PREFIX}/opt/libomp"
    if [[ -d "${LIBOMP_PREFIX}" ]]; then
        LIBOMP_INC="${LIBOMP_PREFIX}/include"
        LIBOMP_LIB="${LIBOMP_PREFIX}/lib/libomp.dylib"
    else
        warn "libomp not found at ${LIBOMP_PREFIX}. OpenMP will not be available."
        LIBOMP_PREFIX=""
    fi
fi

# libxml2 from Homebrew (macOS ships an old one; brew's is newer and includes headers)
LIBXML2_PREFIX=""
if [[ -n "$BREW_PREFIX" ]]; then
    if [[ -f "${BREW_PREFIX}/opt/libxml2/lib/cmake/libxml2/libxml2-config.cmake" ]] || \
       [[ -f "${BREW_PREFIX}/opt/libxml2/lib/pkgconfig/libxml-2.0.pc" ]]; then
        LIBXML2_PREFIX="${BREW_PREFIX}/opt/libxml2"
    fi
fi

# yaml-cpp, gsl — found automatically if Homebrew is in CMAKE_PREFIX_PATH
YAML_PREFIX="${BREW_PREFIX}/opt/yaml-cpp"
GSL_PREFIX="${BREW_PREFIX}/opt/gsl"

# =============================================================================
# 3. Build RPATH list
# Everything in PREFIX/lib must be searchable, plus Qt and libomp.
# =============================================================================
RPATH_LIST="${PREFIX}/lib"
[[ -n "${QT_LIB_DIR:-}" ]]      && RPATH_LIST="${RPATH_LIST};${QT_LIB_DIR}"
[[ -n "${LIBOMP_PREFIX}" ]]     && RPATH_LIST="${RPATH_LIST};${LIBOMP_PREFIX}/lib"
[[ -n "${BREW_PREFIX}" ]]       && RPATH_LIST="${RPATH_LIST};${BREW_PREFIX}/lib"
# Homebrew opt dirs for keg-only formulae (libxml2, yaml-cpp, gsl)
for _keg in libxml2 yaml-cpp gsl; do
    _keg_lib="${BREW_PREFIX}/opt/${_keg}/lib"
    [[ -d "$_keg_lib" ]] && RPATH_LIST="${RPATH_LIST};${_keg_lib}"
done

log "RPATH list: ${RPATH_LIST}"

# =============================================================================
# 4. Assemble reusable CMake flag groups
# =============================================================================

# OpenMP hint flags for Apple Clang (find_package(OpenMP) needs these)
OMP_FLAGS=()
if [[ -n "${LIBOMP_LIB}" ]] && [[ -f "${LIBOMP_LIB}" ]]; then
    # Apple Clang does not include OpenMP; these hints point CMake's
    # find_package(OpenMP) to the Homebrew libomp install.
    # Each flag is passed as a single cmake argument — no embedded spaces.
    OMP_FLAGS=(
        "-DOpenMP_C_FLAGS=-Xpreprocessor;-fopenmp;-I${LIBOMP_INC}"
        "-DOpenMP_C_LIB_NAMES=omp"
        "-DOpenMP_CXX_FLAGS=-Xpreprocessor;-fopenmp;-I${LIBOMP_INC}"
        "-DOpenMP_CXX_LIB_NAMES=omp"
        "-DOpenMP_omp_LIBRARY=${LIBOMP_LIB}"
        "-DCMAKE_EXE_LINKER_FLAGS=-L${LIBOMP_PREFIX}/lib"
        "-DCMAKE_SHARED_LINKER_FLAGS=-L${LIBOMP_PREFIX}/lib"
    )
    log "OpenMP: using libomp from ${LIBOMP_PREFIX}"
else
    warn "libomp not available — packages will build without OpenMP (slower)."
fi

# GPU backends: all off on macOS
GPU_FLAGS=(
    -DUSE_CUDA=OFF
    -DUSE_HIP=OFF
    -DUSE_SYCL=OFF
)

# Qt flag
QT_FLAG=()
[[ -n "$QT_DIR" ]] && QT_FLAG=("-DQt6_DIR=${QT_DIR}")

# CMAKE_PREFIX_PATH additions for keg-only Homebrew formulae
BREW_CMAKE_PREFIX="${PREFIX}"
for _p in "${BREW_PREFIX}" "${BREW_PREFIX}/opt/libxml2" \
           "${BREW_PREFIX}/opt/yaml-cpp" "${BREW_PREFIX}/opt/gsl" \
           "${BREW_PREFIX}/opt/libsamplerate" "${BREW_PREFIX}/opt/ffmpeg"; do
    [[ -d "$_p" ]] && BREW_CMAKE_PREFIX="${BREW_CMAKE_PREFIX};${_p}"
done

# Combined prefix path flag (used everywhere)
PREFIX_PATH_FLAG="-DCMAKE_PREFIX_PATH=${BREW_CMAKE_PREFIX}"

# =============================================================================
# 5. Verify source trees
# =============================================================================
log "Verifying source trees under ${SOURCE_BASE}/src …"
MISSING=0
for _pkg in nphys-data libklustersshared klusters neuroscope ndmanager \
            ndmanager-plugins klustakwik spikerealign; do
    if [[ ! -f "${SOURCE_BASE}/src/${_pkg}/CMakeLists.txt" ]]; then
        error "Source tree not found: ${SOURCE_BASE}/src/${_pkg}"
        MISSING=1
    fi
done
if [[ $MISSING -eq 1 ]]; then
    error "Run this script from the neurosuite-3 repository root, or pass --source-dir."
    exit 1
fi
success "All source trees found."

mkdir -p "${BUILD_BASE}"

# =============================================================================
# 6. Record start time
# =============================================================================
T_START=$(date +%s)

# =============================================================================
# 1/8  nphys-data  (no deps — just installs MIME types and icons)
# Note: on macOS the hicolor icon paths are Linux conventions and are not
# integrated with the OS, but the install is harmless.
# =============================================================================
cmake_build "nphys-data" "${SOURCE_BASE}/src/nphys-data" \
    "${PREFIX_PATH_FLAG}"

# =============================================================================
# 2/8  libklustersshared
# Deps: Qt6 (Core, Gui, Widgets), yaml-cpp
# =============================================================================
cmake_build "libklustersshared" "${SOURCE_BASE}/src/libklustersshared" \
    "${PREFIX_PATH_FLAG}" \
    "${QT_FLAG[@]+"${QT_FLAG[@]}"}"

# =============================================================================
# 3/8  klusters
# Deps: Qt6 (Core, Gui, Widgets, Xml, PrintSupport), libklustersshared
# Optional: OpenMP (via libomp), GPU backends (disabled on macOS)
# =============================================================================
cmake_build "klusters" "${SOURCE_BASE}/src/klusters" \
    "${PREFIX_PATH_FLAG}" \
    "${QT_FLAG[@]+"${QT_FLAG[@]}"}" \
    "${OMP_FLAGS[@]+"${OMP_FLAGS[@]}"}" \
    "${GPU_FLAGS[@]}"

# =============================================================================
# 4/8  neuroscope
# Deps: Qt6 (Core, Gui, Widgets, Xml, PrintSupport), libklustersshared
# =============================================================================
cmake_build "neuroscope" "${SOURCE_BASE}/src/neuroscope" \
    "${PREFIX_PATH_FLAG}" \
    "${QT_FLAG[@]+"${QT_FLAG[@]}"}"

# =============================================================================
# 5/8  ndmanager
# Deps: Qt6 (Core, Gui, Widgets, Xml), libklustersshared
# =============================================================================
cmake_build "ndmanager" "${SOURCE_BASE}/src/ndmanager" \
    "${PREFIX_PATH_FLAG}" \
    "${QT_FLAG[@]+"${QT_FLAG[@]}"}"

# =============================================================================
# 6/8  ndmanager-plugins
# Deps: LibXml2 (required), GSL (required), pkg-config (required)
# Optional: OpenMP, libsamplerate (brew install --with-libsamplerate),
#           FFmpeg (brew install --with-ffmpeg), CUDA (disabled)
# No Qt6 or libklustersshared dependency.
# =============================================================================
cmake_build "ndmanager-plugins" "${SOURCE_BASE}/src/ndmanager-plugins" \
    "${PREFIX_PATH_FLAG}" \
    "${OMP_FLAGS[@]+"${OMP_FLAGS[@]}"}" \
    -DUSE_CUDA=OFF

# =============================================================================
# 7/8  klustakwik
# Deps: OpenMP (optional). GPU backends disabled on macOS.
# NOTE: The CMakeLists will auto-select icpx if oneAPI is installed and
#       USE_SYCL is not explicitly OFF. We pass USE_SYCL=OFF to prevent that.
# =============================================================================
cmake_build "klustakwik" "${SOURCE_BASE}/src/klustakwik" \
    "${PREFIX_PATH_FLAG}" \
    "${OMP_FLAGS[@]+"${OMP_FLAGS[@]}"}" \
    "${GPU_FLAGS[@]}"

# =============================================================================
# 8/8  spikerealign
# Deps: OpenMP (optional). GPU backends disabled on macOS.
# =============================================================================
cmake_build "spikerealign" "${SOURCE_BASE}/src/spikerealign" \
    "${PREFIX_PATH_FLAG}" \
    "${OMP_FLAGS[@]+"${OMP_FLAGS[@]}"}" \
    "${GPU_FLAGS[@]}"

# =============================================================================
# Summary
# =============================================================================
T_END=$(date +%s)
T_ELAPSED=$(( T_END - T_START ))
T_MIN=$(( T_ELAPSED / 60 ))
T_SEC=$(( T_ELAPSED % 60 ))

header "Build complete"
success "All packages built in ${T_MIN}m ${T_SEC}s."

if ${DO_INSTALL}; then
    success "Installed to: ${PREFIX}"

    if ${CLEAN}; then
        success "Build trees removed (--clean)."
    else
        log "Build trees retained in: ${BUILD_BASE}"
        log "(Re-run with --clean to remove, or delete manually.)"
    fi

    echo ""
    log "─── Next steps ──────────────────────────────────────────────"
    echo ""

    # PATH
    if [[ ":${PATH}:" != *":${PREFIX}/bin:"* ]]; then
        log "1. Add NeuroSuite to your PATH:"
        echo "      echo 'export PATH=\"${PREFIX}/bin:\$PATH\"' >> ~/.zprofile"
        echo "      source ~/.zprofile"
        echo ""
    else
        log "1. ${PREFIX}/bin is already on PATH."
        echo ""
    fi

    # DYLD_LIBRARY_PATH / rpath note
    log "2. The installed executables embed RPATH entries so they find"
    log "   libklustersshared and Qt at runtime without extra env vars."
    log "   If a binary reports a missing dylib, check with:"
    echo "      otool -L ${PREFIX}/bin/klusters"
    echo ""

    # Qt runtime note (Homebrew Qt is not a framework bundle)
    if [[ -n "${QT_LIB_DIR:-}" ]]; then
        if [[ ":${DYLD_LIBRARY_PATH:-}:" != *":${QT_LIB_DIR}:"* ]]; then
            log "3. If Qt dylibs are not found at runtime, add to DYLD_LIBRARY_PATH:"
            echo "      export DYLD_LIBRARY_PATH=\"${QT_LIB_DIR}:\$DYLD_LIBRARY_PATH\""
            echo "   Or add permanently in ~/.zprofile."
            echo ""
        fi
    fi

    # Verify
    log "4. Verify the installation (open a new terminal first):"
    echo "      klusters --version"
    echo "      neuroscope --version"
    echo "      ndmanager --version"
    echo "      KlustaKwik"
    echo "      SpikeRealign"
    echo ""
fi
