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
#                        Zen 5; AVX2 + BMI2 on older hosts).  Binaries are not
#                        portable but that is acceptable for a workstation build.
#   -ffast-math          IEEE754 relaxations (reassociation, no signed-zero,
#                        no NaN/Inf checking).  Acceptable for the numerical
#                        kernels in KlustaKwik and process_resample.
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
if(NOT NS_ZEN_OPT)
    return()
endif()

# ── C / C++ flags ─────────────────────────────────────────────────────────────

set(_zen_cxx_flags "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    list(APPEND _zen_cxx_flags
        -O3
        -march=native
        -ffast-math
        -funroll-loops
    )
endif()

# Append to Release and RelWithDebInfo only; leave Debug untouched.
foreach(_cfg Release RelWithDebInfo)
    foreach(_lang C CXX)
        set(CMAKE_${_lang}_FLAGS_${_cfg}
            "${CMAKE_${_lang}_FLAGS_${_cfg}} ${_zen_cxx_flags}"
            CACHE STRING "${_lang} flags for ${_cfg}" FORCE)
    endforeach()
endforeach()

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

message(STATUS "ZenOptimizations: -O3 -march=native -ffast-math applied to Release builds")
