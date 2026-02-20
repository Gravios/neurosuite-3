#!/usr/bin/env bash
# =============================================================================
# build-neurosuite.sh
# Build and install all Neurosuite Qt6 packages in dependency order.
#
# Build order (dependency graph):
#   1. libklustersshared   — shared library, no upstream deps
#   2. klusters            — depends on libklustersshared
#   3. neuroscope          — depends on libklustersshared
#   4. ndmanager           — depends on libklustersshared
#   5. ndmanager-plugins   — standalone (C/CUDA/OpenMP only, no Qt)
#
# Usage:
#   ./build-neurosuite.sh [OPTIONS]
#
# Options:
#   --prefix DIR     Install prefix (default: /usr/local)
#   --build-dir DIR  Parent directory for build trees (default: ./build)
#   --source-dir DIR Parent directory containing all source trees (default: .)
#   --jobs N         Parallel make jobs (default: nproc)
#   --no-install     Configure and build but skip installation
#   --skip PKG       Skip a package; repeat to skip multiple
#                    Valid names: libklustersshared klusters neuroscope
#                                 ndmanager ndmanager-plugins
#   --cuda-arch LIST Semicolon-separated CUDA arch list (default: auto-detect)
#                    Example: --cuda-arch "86;89;120"
#   --clean          Remove each package's build directory after it is
#                    successfully installed. Has no effect with --no-install.
#   --clean-on-fail  Remove a package's build directory only if its build or
#                    install step fails (useful for CI; leaves trees on success
#                    for incremental rebuilds, removes them on error so a retry
#                    starts from a clean state rather than a broken cache).
#   -h, --help       Show this help
#
# Expected source tree layout (adjust with --source-dir):
#   <source-dir>/
#     libklustersshared-qt6/
#     klusters-qt6/
#     neuroscope-patched/      (or neuroscope-qt6/)
#     ndmanager-patched/       (or ndmanager-qt6/)
#     ndmanager-plugins-qt6/
#
# The script will automatically detect the correct directory name for
# neuroscope and ndmanager (preferring the patched variant).
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
header()  { echo -e "\n${C_BOLD}══════════════════════════════════════════════${C_RESET}"; \
             echo -e "${C_BOLD}  $*${C_RESET}"; \
             echo -e "${C_BOLD}══════════════════════════════════════════════${C_RESET}"; }

# ── Defaults ──────────────────────────────────────────────────────────────────
PREFIX="/usr/local"
BUILD_BASE="$(pwd)/build"
SOURCE_BASE="$(pwd)"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
DO_INSTALL=true
CLEAN=false
CLEAN_ON_FAIL=false
SKIP_PKGS=()
CUDA_ARCH=""

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)      PREFIX="$2";      shift 2 ;;
        --build-dir)   BUILD_BASE="$2";  shift 2 ;;
        --source-dir)  SOURCE_BASE="$2"; shift 2 ;;
        --jobs)        JOBS="$2";        shift 2 ;;
        --no-install)       DO_INSTALL=false;        shift   ;;
        --clean)            CLEAN=true;              shift   ;;
        --clean-on-fail)    CLEAN_ON_FAIL=true;      shift   ;;
        --skip)             SKIP_PKGS+=("$2");       shift 2 ;;
        --cuda-arch)   CUDA_ARCH="$2";   shift 2 ;;
        -h|--help)
            sed -n '3,52p' "$0" | sed 's/^# \?//'
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

# ── Helper: resolve which of several candidate dirs exists ───────────────────
resolve_src() {
    # resolve_src <label> <candidate1> [<candidate2> ...]
    local label="$1"; shift
    for candidate in "$@"; do
        local full="${SOURCE_BASE}/${candidate}"
        if [[ -f "${full}/CMakeLists.txt" ]]; then
            echo "$full"
            return 0
        fi
    done
    error "Cannot find source directory for ${label}."
    error "Looked for (under ${SOURCE_BASE}/):"
    for candidate in "$@"; do error "  ${candidate}"; done
    exit 1
}

# ── Helper: remove a build directory (with sudo if needed) ───────────────────
remove_build() {
    local build="$1"
    local label="$2"
    if [[ -d "${build}" ]]; then
        log "Removing build tree: ${build}"
        if [[ -w "${build}" ]] || [[ -w "$(dirname "${build}")" ]]; then
            rm -rf "${build}"
        else
            sudo rm -rf "${build}"
        fi
        log "Build tree removed for ${label}."
    fi
}

# ── Helper: build + (optionally) install one CMake package ───────────────────
cmake_build() {
    local label="$1"
    local src="$2"
    local build="${BUILD_BASE}/${label}"
    shift 2
    # Any remaining args are passed as extra -D flags to cmake

    header "Building ${label}"
    log "Source : ${src}"
    log "Build  : ${build}"
    log "Prefix : ${PREFIX}"

    mkdir -p "${build}"

    # Inner function so we can trap failure for --clean-on-fail
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
            # Use sudo only if the prefix is not user-writable
            if [[ -w "${PREFIX}" ]] || [[ -w "$(dirname "${PREFIX}")" && ! -e "${PREFIX}" ]]; then
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
        # Run in a subshell so we can catch failure without stopping the script
        if ! ( _do_build_and_install "$@" ); then
            error "${label} failed."
            remove_build "${build}" "${label}"
            exit 1
        fi
    else
        _do_build_and_install "$@"
    fi

    # --clean: remove build tree after a successful install
    if ${CLEAN} && ${DO_INSTALL}; then
        remove_build "${build}" "${label}"
    fi
}

# ── Pre-flight checks ─────────────────────────────────────────────────────────
header "Neurosuite Qt6 Build Script"
log "Prefix     : ${PREFIX}"
log "Build base : ${BUILD_BASE}"
log "Source base: ${SOURCE_BASE}"
log "Jobs       : ${JOBS}"
log "Install    : ${DO_INSTALL}"
log "Clean      : ${CLEAN} (on-fail: ${CLEAN_ON_FAIL})"
[[ ${#SKIP_PKGS[@]} -gt 0 ]] && log "Skipping   : ${SKIP_PKGS[*]}"

for tool in cmake ninja gcc g++; do
    if ! command -v "$tool" &>/dev/null; then
        if [[ "$tool" == "ninja" ]]; then
            warn "ninja not found — cmake will use Makefiles instead"
        else
            error "Required tool not found: ${tool}"
            exit 1
        fi
    fi
done

if ! command -v qmake6 &>/dev/null && ! command -v qt6-config &>/dev/null; then
    if ! cmake --find-package -DNAME=Qt6 -DCOMPILER_ID=GNU -DLANGUAGE=CXX -DMODE=EXIST \
            &>/dev/null 2>&1; then
        warn "Qt6 may not be installed or not on CMAKE_PREFIX_PATH."
        warn "Set CMAKE_PREFIX_PATH or Qt6_DIR if the build fails."
    fi
fi

mkdir -p "${BUILD_BASE}"

# ── Resolve source directories ────────────────────────────────────────────────
SRC_SHARED=$(resolve_src "libklustersshared" \
    "libklustersshared-qt6" "libklustersshared")

SRC_KLUSTERS=$(resolve_src "klusters" \
    "klusters-qt6" "klusters")

SRC_NEUROSCOPE=$(resolve_src "neuroscope" \
    "neuroscope-patched" "neuroscope-qt6" "neuroscope")

SRC_NDMANAGER=$(resolve_src "ndmanager" \
    "ndmanager-patched" "ndmanager-qt6" "ndmanager")

SRC_PLUGINS=$(resolve_src "ndmanager-plugins" \
    "ndmanager-plugins-qt6" "ndmanager-plugins")

# ── Record start time ─────────────────────────────────────────────────────────
T_START=$(date +%s)

# ══════════════════════════════════════════════════════════════════════════════
# 1. libklustersshared
# Must be installed before any downstream package is configured, because
# klusters/neuroscope/ndmanager all call find_package(LibKlustersShared).
# ══════════════════════════════════════════════════════════════════════════════
if should_skip "libklustersshared"; then
    warn "Skipping libklustersshared (--skip requested)"
else
    cmake_build "libklustersshared" "${SRC_SHARED}"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 2. klusters
# ══════════════════════════════════════════════════════════════════════════════
if should_skip "klusters"; then
    warn "Skipping klusters (--skip requested)"
else
    cmake_build "klusters" "${SRC_KLUSTERS}"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 3. neuroscope
# ══════════════════════════════════════════════════════════════════════════════
if should_skip "neuroscope"; then
    warn "Skipping neuroscope (--skip requested)"
else
    cmake_build "neuroscope" "${SRC_NEUROSCOPE}"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. ndmanager
# ══════════════════════════════════════════════════════════════════════════════
if should_skip "ndmanager"; then
    warn "Skipping ndmanager (--skip requested)"
else
    cmake_build "ndmanager" "${SRC_NDMANAGER}"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. ndmanager-plugins
# Standalone: pure C/C++ with optional CUDA and OpenMP.
# No Qt dependency; no libklustersshared dependency.
# CUDA is auto-detected by CMake's check_language(CUDA).
# Override arch list with --cuda-arch "86;89;120" if needed.
# ══════════════════════════════════════════════════════════════════════════════
if should_skip "ndmanager-plugins"; then
    warn "Skipping ndmanager-plugins (--skip requested)"
else
    EXTRA_PLUGIN_FLAGS=()
    if [[ -n "${CUDA_ARCH}" ]]; then
        EXTRA_PLUGIN_FLAGS+=("-DCMAKE_CUDA_ARCHITECTURES=${CUDA_ARCH}")
        log "CUDA arch override: ${CUDA_ARCH}"
    fi
    cmake_build "ndmanager-plugins" "${SRC_PLUGINS}" "${EXTRA_PLUGIN_FLAGS[@]+"${EXTRA_PLUGIN_FLAGS[@]}"}"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
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
    log ""
    log "If ${PREFIX}/lib is not in your library path, add it:"
    log "  echo '${PREFIX}/lib' | sudo tee /etc/ld.so.conf.d/neurosuite.conf"
    log "  sudo ldconfig"
    log ""
    log "If ${PREFIX}/bin is not in PATH:"
    log "  export PATH=\"${PREFIX}/bin:\$PATH\""
fi
