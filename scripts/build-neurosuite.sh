#!/usr/bin/env bash
# =============================================================================
# build-neurosuite.sh
# Build and install all NeuroSuite-3 packages on Linux (Ubuntu 24.04).
#
# Build order (dependency graph):
#   1. nphys-data          — MIME types and icons; no compiler needed
#   2. libklustersshared   — shared Qt6 library; all Qt apps depend on it
#   3. klusters            — depends on libklustersshared
#   4. neuroscope          — depends on libklustersshared
#   5. ndmanager           — depends on libklustersshared
#   6. ndmanager-plugins   — standalone C/C++ (no Qt); optional CUDA/FFmpeg
#   7. kiloklustakwik          — standalone C/C++; optional CUDA/HIP/SYCL
#   8.        — standalone C/C++; optional CUDA/HIP/SYCL
#
# GPU BACKENDS (CUDA / HIP / SYCL)
#   Auto-detected by CMake for kiloklustakwik,, klusters, and the
#   CUDA-accelerated ndmanager-plugins (process_medianfilter, process_spikegrouper,
#   process_medianthreshold).
#   RTX 5070 Ti (Blackwell, sm_120) requires CUDA >= 12.8 and driver >= 570
#   from NVIDIA's own repository — Ubuntu 24.04's stock cuda toolkit is too old.
#   Pass --gpu-off to disable all GPU backends and build CPU-only.
#
# APT PREREQUISITES (Ubuntu 24.04)
#   # Core build tools and libraries
#   sudo apt-get install -y \
#       build-essential cmake ninja-build git pkg-config \
#       qt6-base-dev libgl-dev libxkbcommon-dev libxcb-cursor0 \
#       libyaml-cpp-dev libxml2-dev libgsl-dev libsamplerate0-dev \
#       libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
#       ffmpeg python3
#
#   # CUDA 12.8+ for RTX 5070 Ti (Blackwell / sm_120) — from NVIDIA repo:
#   wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-keyring_1.1-1_all.deb
#   sudo dpkg -i cuda-keyring_1.1-1_all.deb && sudo apt-get update
#   sudo apt-get install -y cuda-toolkit-12-8 nvidia-open
#
# USAGE
#   ./build-neurosuite.sh [OPTIONS]
#
# OPTIONS
#   --prefix     DIR   Install prefix                (default: /usr/local)
#   --build-dir  DIR   Parent of build trees         (default: ./build)
#   --source-dir DIR   src/ directory (default: ./src)
#   --jobs       N     Parallel build jobs           (default: nproc)
#   --cuda-arch  LIST  Semicolon-separated CUDA arch list
#                      (default: auto — targets 86;89;100;120 when nvcc found)
#                      Example: --cuda-arch "86;89;120"
#   --gpu-off          Disable all GPU backends (CUDA / HIP / SYCL)
#   --no-install       Build but do not install
#   --only       PKG   Build only this package; repeat for multiple.
#                      All other packages are skipped. Mutually exclusive
#                      with --skip. Valid names:
#                      nphys-data libklustersshared klusters neuroscope
#                      ndmanager ndmanager-plugins kiloklustakwik
#   --skip       PKG   Skip a package; repeat for multiple. Valid names:
#                      nphys-data libklustersshared klusters neuroscope
#                      ndmanager ndmanager-plugins kiloklustakwik
#   --clean            Remove each package's build tree after install
#   --clean-on-fail    Remove build tree only when a package fails
#   -h, --help         Show this help
#
# EXAMPLES
#   ./build-neurosuite.sh
#   ./build-neurosuite.sh --prefix ~/.local --jobs 8
#   ./build-neurosuite.sh --only klusters
#   ./build-neurosuite.sh --only kiloklustakwik --only --cuda-arch "120"
#   ./build-neurosuite.sh --gpu-off --skip kiloklustakwik --skip
#   ./build-neurosuite.sh --cuda-arch "120" --clean
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
PREFIX="/usr/local"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_BASE="${REPO_ROOT}/build"
SOURCE_BASE="${REPO_ROOT}/src"
JOBS="$(nproc 2>/dev/null || echo 4)"
DO_INSTALL=true
GPU_OFF=false
CLEAN=false
CLEAN_ON_FAIL=false
SKIP_PKGS=()
ONLY_PKGS=()
CUDA_ARCH=""

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)       PREFIX="$2";      shift 2 ;;
        --build-dir)    BUILD_BASE="$2";  shift 2 ;;
        --source-dir)   SOURCE_BASE="$2"; shift 2 ;;
        --jobs)         JOBS="$2";        shift 2 ;;
        --cuda-arch)    CUDA_ARCH="$2";   shift 2 ;;
        --gpu-off)      GPU_OFF=true;     shift   ;;
        --no-install)   DO_INSTALL=false; shift   ;;
        --clean)        CLEAN=true;       shift   ;;
        --clean-on-fail) CLEAN_ON_FAIL=true; shift ;;
        --only)         ONLY_PKGS+=("$2"); shift 2 ;;
        --skip)         SKIP_PKGS+=("$2"); shift 2 ;;
        -h|--help)
            sed -n '3,62p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *) error "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Validate --only / --skip mutual exclusion ─────────────────────────────────
if [[ ${#ONLY_PKGS[@]} -gt 0 ]] && [[ ${#SKIP_PKGS[@]} -gt 0 ]]; then
    error "--only and --skip are mutually exclusive."
    exit 1
fi

# ── Helpers ───────────────────────────────────────────────────────────────────
should_skip() {
    local pkg="$1"
    # --only mode: skip everything not explicitly listed
    if [[ ${#ONLY_PKGS[@]} -gt 0 ]]; then
        for o in "${ONLY_PKGS[@]}"; do
            [[ "$o" == "$pkg" ]] && return 1  # in the list → do not skip
        done
        return 0  # not in the list → skip
    fi
    # --skip mode: skip packages explicitly listed
    for s in "${SKIP_PKGS[@]+"${SKIP_PKGS[@]}"}"; do
        [[ "$s" == "$pkg" ]] && return 0
    done
    return 1
}

remove_build() {
    local build="$1" label="$2"
    [[ -d "${build}" ]] || return 0
    log "Removing build tree: ${build}"
    if [[ -w "${build}" ]] || [[ -w "$(dirname "${build}")" ]]; then
        rm -rf "${build}"
    else
        sudo rm -rf "${build}"
    fi
    log "Build tree removed for ${label}."
}

cmake_build() {
    local label="$1" src="$2"
    local build="${BUILD_BASE}/${label}"
    shift 2

    if should_skip "${label}"; then
        warn "Skipping ${label} (--skip requested)"
        return 0
    fi

    header "Building ${label}"
    log "Source : ${src}"
    log "Build  : ${build}"
    log "Prefix : ${PREFIX}"

    if [[ ! -f "${src}/CMakeLists.txt" ]]; then
        error "Source not found: ${src}"
        exit 1
    fi

    mkdir -p "${build}"

    _do_build_and_install() {
        log "Configuring…"
        cmake -S "${src}" -B "${build}" \
            -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_PREFIX_PATH="${PREFIX}" \
            "$@"

        log "Compiling (jobs: ${JOBS})…"
        cmake --build "${build}" --parallel "${JOBS}"

        if ${DO_INSTALL}; then
            log "Installing…"
            if [[ -w "${PREFIX}" ]] || \
               { [[ -w "$(dirname "${PREFIX}")" ]] && [[ ! -e "${PREFIX}" ]]; }; then
                cmake --install "${build}"
            else
                warn "Prefix ${PREFIX} not writable — using sudo for install"
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
# Pre-flight checks
# =============================================================================
header "NeuroSuite-3 Linux Build Script"
log "Prefix     : ${PREFIX}"
log "Build base : ${BUILD_BASE}"
log "Source base: ${SOURCE_BASE}"
log "Jobs       : ${JOBS}"
log "Install    : ${DO_INSTALL}"
log "GPU off    : ${GPU_OFF}"
log "CUDA arch  : ${CUDA_ARCH:-auto}"
log "Clean      : ${CLEAN} (on-fail: ${CLEAN_ON_FAIL})"
[[ ${#ONLY_PKGS[@]} -gt 0 ]] && log "Only       : ${ONLY_PKGS[*]}"
[[ ${#SKIP_PKGS[@]} -gt 0 ]] && log "Skipping   : ${SKIP_PKGS[*]}"

# Required tools
for tool in cmake g++ git pkg-config; do
    if ! command -v "${tool}" &>/dev/null; then
        error "Required tool not found: ${tool}"
        error "Install with: sudo apt-get install -y build-essential cmake git pkg-config"
        exit 1
    fi
done
success "Build toolchain found (cmake $(cmake --version | head -1 | awk '{print $3}'), $(g++ --version | head -1))"

# Warn if ninja is absent (cmake falls back to Makefiles — still works)
if ! command -v ninja &>/dev/null; then
    warn "ninja not found — CMake will use Makefiles (install ninja-build for faster builds)"
fi

# Qt6 sanity check
if ! pkg-config --exists Qt6Core 2>/dev/null && \
   ! dpkg -l qt6-base-dev &>/dev/null 2>&1; then
    warn "qt6-base-dev may not be installed. Qt-dependent packages will fail."
    warn "Install with: sudo apt-get install -y qt6-base-dev libgl-dev libxkbcommon-dev"
fi

# CUDA check (informational only — packages auto-detect at configure time)
if command -v nvcc &>/dev/null; then
    NVCC_VER=$(nvcc --version | grep -oP 'release \K[0-9.]+')
    success "CUDA found: nvcc ${NVCC_VER}"
    if [[ -n "${NVCC_VER}" ]]; then
        NVCC_MAJOR="${NVCC_VER%%.*}"
        if (( NVCC_MAJOR < 12 )); then
            warn "CUDA ${NVCC_VER} detected. RTX 5070 Ti (sm_120) requires CUDA >= 12.8."
            warn "Install cuda-toolkit-12-8 from NVIDIA's repository."
        fi
    fi
else
    if ${GPU_OFF}; then
        log "nvcc not found — GPU backends disabled (--gpu-off)."
    else
        warn "nvcc not found — CUDA backends will be skipped by CMake."
        warn "For RTX 5070 Ti support, install cuda-toolkit-12-8 from NVIDIA's repo."
    fi
fi

# Verify all source trees exist under src/
log "Verifying source trees under ${SOURCE_BASE} …"
MISSING=0
for pkg in nphys-data libklustersshared klusters neuroscope ndmanager \
           ndmanager-plugins kiloklustakwik; do
    if [[ ! -f "${SOURCE_BASE}/${pkg}/CMakeLists.txt" ]]; then
        error "Source tree not found: ${SOURCE_BASE}/${pkg}"
        MISSING=1
    fi
done
if [[ "${MISSING}" -eq 1 ]]; then
    error "Run this script from the neurosuite-3 repository root, or pass --source-dir <repo>/src."
    exit 1
fi
success "All source trees found."

mkdir -p "${BUILD_BASE}"

# ── GPU flag assembly ─────────────────────────────────────────────────────────
GPU_FLAGS=()
if ${GPU_OFF}; then
    GPU_FLAGS=(-DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF)
    log "GPU backends: disabled (--gpu-off)"
fi
CUDA_ARCH_FLAG=()
if [[ -n "${CUDA_ARCH}" ]]; then
    CUDA_ARCH_FLAG=("-DCMAKE_CUDA_ARCHITECTURES=${CUDA_ARCH}")
    log "CUDA arch override: ${CUDA_ARCH}"
fi

# ── Record start time ─────────────────────────────────────────────────────────
T_START=$(date +%s)

# =============================================================================
# 1/7  nphys-data
# No compiler required. Installs MIME types and hicolor icons.
# update-mime-database and gtk-update-icon-cache are called post-install
# by the system if xdg-utils is present.
# =============================================================================
cmake_build "nphys-data" "${SOURCE_BASE}/nphys-data"

# =============================================================================
# 2/7  libklustersshared
# Deps: Qt6 (Core, Gui, Widgets), yaml-cpp
# Must be fully installed before klusters, neuroscope, and ndmanager are
# configured — they all call find_package(LibKlustersShared 2.1.0 REQUIRED).
# =============================================================================
cmake_build "libklustersshared" "${SOURCE_BASE}/libklustersshared"

# =============================================================================
# 3/7  klusters
# Deps: Qt6 (Core, Gui, Widgets, Xml, PrintSupport), libklustersshared
# Optional: OpenMP (auto, via libgomp bundled with GCC), CUDA, HIP, SYCL
# =============================================================================
cmake_build "klusters" "${SOURCE_BASE}/klusters" \
    "${GPU_FLAGS[@]+"${GPU_FLAGS[@]}"}" \
    "${CUDA_ARCH_FLAG[@]+"${CUDA_ARCH_FLAG[@]}"}"

# =============================================================================
# 4/7  neuroscope
# Deps: Qt6 (Core, Gui, Widgets, Xml, PrintSupport), libklustersshared
# =============================================================================
cmake_build "neuroscope" "${SOURCE_BASE}/neuroscope"

# =============================================================================
# 5/7  ndmanager
# Deps: Qt6 (Core, Gui, Widgets, Xml), libklustersshared
# =============================================================================
cmake_build "ndmanager" "${SOURCE_BASE}/ndmanager"

# =============================================================================
# 6/7  ndmanager-plugins
# Deps: LibXml2 (REQUIRED), GSL (REQUIRED), pkg-config
# Optional: OpenMP, libsamplerate (system pkg recommended over vendored 0.1.8),
#           FFmpeg/libav (process_extractleds), CUDA (process_medianfilter,
#           process_medianthreshold, process_spikegrouper)
# No Qt6 or libklustersshared dependency.
# =============================================================================
cmake_build "ndmanager-plugins" "${SOURCE_BASE}/ndmanager-plugins" \
    "${CUDA_ARCH_FLAG[@]+"${CUDA_ARCH_FLAG[@]}"}" \
    "${GPU_FLAGS[@]+"${GPU_FLAGS[@]}"}"

# =============================================================================
# 7/7  kiloklustakwik
# Deps: OpenMP (optional, auto via GCC libgomp)
# Optional GPU: CUDA > HIP > SYCL (auto-detected, priority order)
# NOTE: If Intel oneAPI is installed and USE_SYCL is not explicitly OFF,
#       CMakeLists.txt will auto-select icpx as C++ compiler before project().
#       Pass --gpu-off to force GCC and avoid this.
# =============================================================================
cmake_build "kiloklustakwik" "${SOURCE_BASE}/kiloklustakwik" \
    "${GPU_FLAGS[@]+"${GPU_FLAGS[@]}"}" \
    "${CUDA_ARCH_FLAG[@]+"${CUDA_ARCH_FLAG[@]}"}"

# =============================================================================
# Post-install
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
        log "Build trees kept in: ${BUILD_BASE}"
        log "(Re-run with --clean to remove them, or delete manually.)"
    fi

    # ── ldconfig — register the new shared library ────────────────────────────
    # libklustersshared installs its .so into ${PREFIX}/lib. Tell the dynamic
    # linker about it so klusters, neuroscope and ndmanager can find it at
    # runtime without requiring LD_LIBRARY_PATH.
    if [[ "${PREFIX}" == /usr* ]] || [[ "${PREFIX}" == /opt* ]]; then
        LDCONF_FILE="/etc/ld.so.conf.d/neurosuite.conf"
        if [[ ! -f "${LDCONF_FILE}" ]] || \
           ! grep -qF "${PREFIX}/lib" "${LDCONF_FILE}" 2>/dev/null; then
            log "Registering ${PREFIX}/lib with ldconfig…"
            echo "${PREFIX}/lib" | sudo tee "${LDCONF_FILE}" > /dev/null
        fi
        sudo ldconfig
        success "ldconfig updated."
    else
        warn "Non-system prefix (${PREFIX}) — ldconfig not run automatically."
        warn "Add ${PREFIX}/lib to your library path:"
        log "  export LD_LIBRARY_PATH=\"${PREFIX}/lib:\${LD_LIBRARY_PATH:-}\""
        log "  # or add permanently:"
        log "  echo '${PREFIX}/lib' | sudo tee /etc/ld.so.conf.d/neurosuite.conf"
        log "  sudo ldconfig"
    fi

    # ── avconv compatibility symlink ──────────────────────────────────────────
    # ndm_transcodevideo calls 'avconv', which was replaced by 'ffmpeg'.
    # Create a symlink so the script works out of the box.
    if command -v ffmpeg &>/dev/null && ! command -v avconv &>/dev/null; then
        FFMPEG_PATH="$(command -v ffmpeg)"
        AVCONV_DEST="/usr/local/bin/avconv"
        log "Creating avconv → ffmpeg symlink for ndm_transcodevideo…"
        if [[ -w "$(dirname "${AVCONV_DEST}")" ]]; then
            ln -sf "${FFMPEG_PATH}" "${AVCONV_DEST}"
        else
            sudo ln -sf "${FFMPEG_PATH}" "${AVCONV_DEST}"
        fi
        success "avconv symlink created: ${AVCONV_DEST} → ${FFMPEG_PATH}"
    fi

    # ── PATH and verification hints ───────────────────────────────────────────
    echo ""
    log "─── Next steps ──────────────────────────────────────────────"
    echo ""

    if [[ ":${PATH}:" != *":${PREFIX}/bin:"* ]]; then
        log "Add NeuroSuite to PATH (open a new terminal after):"
        echo "    echo 'export PATH=\"${PREFIX}/bin:\$PATH\"' >> ~/.bashrc"
        echo "    source ~/.bashrc"
        echo ""
    fi

    log "Verify the installation:"
    echo "    klusters --version"
    echo "    neuroscope --version"
    echo "    ndmanager --version"
    echo "    KiloKlustaKwik"
    echo "    SpikeRealign"
    echo ""
fi
