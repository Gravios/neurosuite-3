# =============================================================================
# ZenOptimizations.cmake
#
# Global compiler/linker optimization flags for AMD Zen 4/5 workstations
# (specifically tuned for the Ryzen 7 9800X3D used as the primary build host).
#
# Included from the top-level CMakeLists.txt before any add_subdirectory().
# Only takes effect when the build type is Release or RelWithDebInfo; Debug
# builds are untouched so that sanitisers and debuggers still work.
#
# Features enabled
# ────────────────
#   -march=native        Full ISA of the build host (AVX-512 + BF16 + VNNI on
#                        Zen 5; AVX2 + BMI2 on older hosts).  Applied GLOBALLY
#                        and ON BY DEFAULT (opt-out via -DNS_NATIVE_ARCH=OFF):
#                        the project ships no binaries, so building for the host
#                        CPU is the right default and non-portability is moot.
#   -ffast-math          IEEE754 relaxations (reassociation, no signed-zero,
#                        no NaN/Inf checking).  NOT global — exposed as the
#                        opt-in INTERFACE target ns_fast_math so only numeric
#                        kernels (e.g. KlustaKwik's CEM core, process_resample)
#                        take it; the GUI and guard-heavy code stay strict.
#   -funroll-loops       Unroll small loops; pairs well with AVX-512 vectoriser.
#   IPO/LTO              Inter-procedural / link-time optimisation in Release
#                        mode.  Catches cross-TU inlining opportunities that
#                        are significant in the Qt GUI components.
#   --threads N (nvcc)   Parallel CUDA device-code compilation.  Cuts CUDA
#                        build time proportionally to the number of SM targets.
#   -Xptxas -dlcm=ca     Prefer cached global loads in CUDA kernels (L2-first
#                        vs streaming).  Beneficial for KlustaKwik's covariance
#                        kernels which reuse data across threads.
#
# Usage
# ─────
#   include(cmake/ZenOptimizations.cmake)   # in root CMakeLists.txt
#
# To disable (e.g. when cross-compiling for a different host):
#   cmake -B build -DNS_ZEN_OPT=OFF
# =============================================================================

option(NS_ZEN_OPT "Apply Zen-optimized compiler flags globally" ON)

# -march=native is applied GLOBALLY and ON BY DEFAULT (opt-out).  Rationale:
# neurosuite-3 ships no binaries — every user compiles it on their own machine —
# so building for the host CPU's full ISA is the right default and the
# non-portability of the result is irrelevant.  Set -DNS_NATIVE_ARCH=OFF for a
# portable or cross-compiled build (e.g. building on one machine to run on
# another, or for a reproducible/redistributable artefact).
option(NS_NATIVE_ARCH
    "Compile for the build host's native ISA (-march=native). ON by default; OFF for portable/cross builds."
    ON)

# ── Opt-in fast-math bundle ───────────────────────────────────────────────────
# -ffast-math is intentionally NOT applied globally.  It is value-unsafe (reassociation,
# no-NaN/Inf assumption via -ffinite-math-only, no signed zero) and, when it
# reaches a link line, pulls in crtfastmath.o which sets FTZ/DAZ process-wide.
# Applied to every TU it can silently delete NaN/Inf guards and perturb results
# in the GUI and the numerically sensitive non-kernel code.
#
# Instead it is exposed as an INTERFACE target that numeric kernels opt into:
#       target_link_libraries(<kernel-target> PRIVATE ns_fast_math)
#
# Defined UNCONDITIONALLY (before the NS_ZEN_OPT early-return below) so that
# consumers can always link it; when NS_ZEN_OPT=OFF or the build type is Debug
# the generator expression collapses to nothing and it is a harmless no-op.
# The COMPILE_LANGUAGE:CXX guard keeps the flag off CUDA/SYCL device TUs (nvcc
# would need -Xcompiler; device code uses --use_fast_math separately).
if(NOT TARGET ns_fast_math)
    add_library(ns_fast_math INTERFACE)
endif()
# Options gated on NS_ZEN_OPT so that NS_ZEN_OPT=OFF yields a fully strict,
# portable build everywhere (also the one-flag way to A/B whether fast-math is
# perturbing drift/PCA results).  The target stays defined either way, so
# consumers can link it unconditionally.
if(NS_ZEN_OPT)
    target_compile_options(ns_fast_math INTERFACE
        $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-ffast-math>)
endif()

# ── ns_native_arch — retained for source compatibility ───────────────────────
# -march=native is now applied GLOBALLY by default (NS_NATIVE_ARCH, opt-out)
# further down, so this per-target opt-in is no longer the mechanism.  The target
# is kept as an empty INTERFACE library so the existing
# `target_link_libraries(<t> PRIVATE ns_native_arch)` lines in the plugins keep
# resolving without churn; it now contributes no flags.  To force native on a
# single target inside an otherwise-portable (NS_NATIVE_ARCH=OFF) build, add
# -march=native to that target directly.
if(NOT TARGET ns_native_arch)
    add_library(ns_native_arch INTERFACE)
endif()
# NaN/Inf-safe variant: if a kernel must keep NaN/Inf usable as sentinels while
# still getting reassociation/vectorisation, link a target that adds
# "-ffast-math -fno-finite-math-only" instead — -ffinite-math-only is the part
# that deletes guards.

if(NOT NS_ZEN_OPT)
    return()
endif()

# ── Global compile options (Release / RelWithDebInfo, GNU/Clang-family) ───────
# Applied via add_compile_options() so they actually reach every target in every
# subdirectory.  The previous set(CMAKE_<LANG>_FLAGS_<CFG> ... CACHE STRING ...
# FORCE) loop was a verified no-op — a normal-variable shadow kept the default,
# so none of the flags ever reached a compile line (confirmed by inspecting the
# generated flags.make).  add_compile_options is the correct directory-scoped
# mechanism and, included here before any add_subdirectory(), applies tree-wide.
#
#   -funroll-loops   portable; always applied under NS_ZEN_OPT.
#   -march=native    host-specific (non-portable binaries) but ON BY DEFAULT
#                    via NS_NATIVE_ARCH — see the option() above.  Pass
#                    -DNS_NATIVE_ARCH=OFF for a portable/cross build.
#   -O3              not injected here: it is already in the Release/
#                    RelWithDebInfo default flags.
#
# COMPILE_LANGUAGE is restricted to C and CXX so nvcc / HIP device passes never
# receive these host-only flags (device code handles its own optimisation).
set(_ns_rel "$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>")

add_compile_options(
    "$<$<AND:$<COMPILE_LANGUAGE:CXX>,${_ns_rel},$<CXX_COMPILER_ID:GNU,Clang,AppleClang,IntelLLVM>>:-funroll-loops>"
    "$<$<AND:$<COMPILE_LANGUAGE:C>,${_ns_rel},$<C_COMPILER_ID:GNU,Clang,AppleClang,IntelLLVM>>:-funroll-loops>")

if(NS_NATIVE_ARCH)
    add_compile_options(
        "$<$<AND:$<COMPILE_LANGUAGE:CXX>,${_ns_rel},$<CXX_COMPILER_ID:GNU,Clang,AppleClang,IntelLLVM>>:-march=native>"
        "$<$<AND:$<COMPILE_LANGUAGE:C>,${_ns_rel},$<C_COMPILER_ID:GNU,Clang,AppleClang,IntelLLVM>>:-march=native>")
    message(STATUS "ZenOptimizations: -march=native applied globally "
                   "(NS_NATIVE_ARCH=ON — pass -DNS_NATIVE_ARCH=OFF for a portable build)")
else()
    message(STATUS "ZenOptimizations: portable build (NS_NATIVE_ARCH=OFF) — no -march=native")
endif()

# ── IPO / LTO ─────────────────────────────────────────────────────────────────
# Link-time optimisation; check that the toolchain supports it before enabling.
#
# With the Intel oneAPI compiler (icpx/icx), CMake's check_ipo_supported() may
# return TRUE even though CMAKE_CXX_COMPILER_AR was not resolved, because CMake
# does not automatically locate xiar (Intel's LTO-aware archiver).  Without it
# the LTO link step fails with a literal "CMAKE_CXX_COMPILER_AR-NOTFOUND"
# command.  We therefore:
#   1. For Intel compilers, probe for xiar next to icpx and set CMAKE_AR and
#      CMAKE_CXX_COMPILER_AR before running check_ipo_supported().
#   2. After check_ipo_supported() succeeds, verify the archiver is not NOTFOUND
#      before actually enabling IPO; if it is still unresolved, skip silently.

if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    # For the modern LLVM-based Intel compiler (icpx), the LTO archiver is
    # llvm-ar, located in a "compiler/" subdirectory alongside icpx:
    #   .../compiler/<version>/bin/icpx
    #   .../compiler/<version>/bin/compiler/llvm-ar
    # The old Classic Compiler used xiar; keep it as a fallback.
    get_filename_component(_icpx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(_intel_ar
        NAMES llvm-ar xiar
        HINTS "${_icpx_dir}/compiler" "${_icpx_dir}"
        NO_DEFAULT_PATH)
    if(_intel_ar)
        set(CMAKE_AR              "${_intel_ar}" CACHE FILEPATH "Intel LTO archiver" FORCE)
        set(CMAKE_CXX_COMPILER_AR "${_intel_ar}" CACHE FILEPATH "Intel LTO archiver (CXX)" FORCE)
        set(CMAKE_C_COMPILER_AR   "${_intel_ar}" CACHE FILEPATH "Intel LTO archiver (C)"   FORCE)
        # Matching ranlib: prefer llvm-ranlib next to llvm-ar, fall back to system ranlib.
        get_filename_component(_ar_dir "${_intel_ar}" DIRECTORY)
        find_program(_intel_ranlib
            NAMES llvm-ranlib ranlib
            HINTS "${_ar_dir}"
            NO_DEFAULT_PATH)
        if(_intel_ranlib)
            set(CMAKE_RANLIB              "${_intel_ranlib}" CACHE FILEPATH "" FORCE)
            set(CMAKE_CXX_COMPILER_RANLIB "${_intel_ranlib}" CACHE FILEPATH "" FORCE)
            set(CMAKE_C_COMPILER_RANLIB   "${_intel_ranlib}" CACHE FILEPATH "" FORCE)
        endif()
        message(STATUS "ZenOptimizations: Intel LTO archiver: ${_intel_ar}")
        unset(_ar_dir)
        unset(_intel_ranlib CACHE)
    else()
        # Neither llvm-ar nor xiar found — skip the IPO probe entirely.
        # check_ipo_supported() with icpx compiles with -ipo successfully but
        # then tries to archive with CMAKE_CXX_COMPILER_AR-NOTFOUND, producing
        # a noisy failure.  Early-out here to keep the configure log clean.
        message(STATUS "ZenOptimizations: Intel LTO archiver (llvm-ar/xiar) not found — IPO/LTO disabled")
        unset(_icpx_dir)
        unset(_intel_ar CACHE)
        return()
    endif()
    unset(_icpx_dir)
    unset(_intel_ar CACHE)
endif()

include(CheckIPOSupported OPTIONAL RESULT_VARIABLE _ipo_module)
if(_ipo_module)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_msg)
    # Double-check: even if the probe succeeded, the archiver must be resolved.
    # CMAKE_CXX_COMPILER_AR can be set to NOTFOUND as a string, not an empty var.
    if(_ipo_ok
            AND CMAKE_CXX_COMPILER_AR
            AND NOT CMAKE_CXX_COMPILER_AR MATCHES "-NOTFOUND$")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE         ON CACHE BOOL "" FORCE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO  ON CACHE BOOL "" FORCE)
        message(STATUS "ZenOptimizations: IPO/LTO enabled for Release builds")
    elseif(_ipo_ok)
        message(STATUS "ZenOptimizations: IPO/LTO probe passed but archiver unresolved — skipping")
    else()
        message(STATUS "ZenOptimizations: IPO/LTO not supported — skipped")
    endif()
endif()

# ── CUDA flags ────────────────────────────────────────────────────────────────
# Additional nvcc flags on top of what each target already sets.
# --threads 0    = nvcc picks thread count equal to CPU core count (parallel
#                  device code compilation — major build-time speedup).
# -Xptxas -dlcm=ca  = L2-cached global loads (better for covariance kernels).

if(CMAKE_CUDA_COMPILER)
    set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --threads 0 -Xptxas -dlcm=ca"
        CACHE STRING "Global CUDA flags" FORCE)
    message(STATUS "ZenOptimizations: CUDA --threads 0 -Xptxas -dlcm=ca enabled")
endif()

message(STATUS "ZenOptimizations: -funroll-loops applied to Release builds; "
               "-march=native ${NS_NATIVE_ARCH} (global, opt-out); "
               "-ffast-math is opt-in per target via ns_fast_math")
