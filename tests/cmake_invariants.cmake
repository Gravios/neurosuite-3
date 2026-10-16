# =============================================================================
# cmake_invariants.cmake — static regression checks for the build system
#
# Guards the build-system invariants established during the CMake audit so a
# future edit cannot silently undo them.  Pure static checks (read the source
# tree, assert properties) — no configure or toolchain required.
#
# Run directly:   cmake -DREPO=<repo-root> -P tests/cmake_invariants.cmake
# Run via ctest:  registered as the `build_invariants` test (NS_BUILD_TESTS=ON).
# Exits non-zero (FATAL_ERROR) if any invariant fails.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED REPO)
    get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(_failed "")
macro(_ok msg)
    message(STATUS "  ok    ${msg}")
endmacro()
macro(_no msg)
    message(STATUS "  FAIL  ${msg}")
    list(APPEND _failed "${msg}")
endmacro()

function(_slurp out path)
    if(EXISTS "${path}")
        file(READ "${path}" _c)
    else()
        set(_c "")
    endif()
    set(${out} "${_c}" PARENT_SCOPE)
endfunction()

message(STATUS "build-system invariants (REPO=${REPO})")

# --- 1. -march=native is a global opt-out (NS_NATIVE_ARCH) -------------------
_slurp(zen "${REPO}/cmake/ZenOptimizations.cmake")
if(zen MATCHES "option\\(NS_NATIVE_ARCH")
    _ok("NS_NATIVE_ARCH option declared")
else()
    _no("NS_NATIVE_ARCH option missing from ZenOptimizations.cmake")
endif()
# applied through add_compile_options gated on the option (not the old dead
# set(CMAKE_<LANG>_FLAGS_<CFG> ... CACHE FORCE) block)
if(zen MATCHES "if\\(NS_NATIVE_ARCH\\)" AND zen MATCHES "add_compile_options" AND zen MATCHES "march=native")
    _ok("native applied via add_compile_options gated on NS_NATIVE_ARCH")
else()
    _no("native not applied via NS_NATIVE_ARCH-gated add_compile_options")
endif()
if(zen MATCHES "CMAKE_[A-Z]+_FLAGS_[A-Za-z]+[^\n]*CACHE[^\n]*FORCE")
    _no("dead set(CMAKE_<LANG>_FLAGS_<CFG> ... CACHE FORCE) block reintroduced")
else()
    _ok("no dead cache-FORCE flag block")
endif()

# --- 2. ns_native_arch shim fully removed -----------------------------------
file(GLOB_RECURSE _files LIST_DIRECTORIES false "${REPO}/src/*" "${REPO}/cmake/*")
list(APPEND _files "${REPO}/CMakeLists.txt")
set(_offenders "")
foreach(_f ${_files})
    if((_f MATCHES "CMakeLists\\.txt$" OR _f MATCHES "\\.cmake$") AND NOT _f MATCHES "libsamplerate")
        file(READ "${_f}" _c)
        if(_c MATCHES "ns_native_arch")
            file(RELATIVE_PATH _rel "${REPO}" "${_f}")
            list(APPEND _offenders "${_rel}")
        endif()
    endif()
endforeach()
if(_offenders STREQUAL "")
    _ok("ns_native_arch referenced nowhere")
else()
    _no("ns_native_arch still referenced in: ${_offenders}")
endif()

# --- 3. shared GPU-backend module present and complete ----------------------
_slurp(gpu "${REPO}/cmake/GpuBackends.cmake")
if(gpu STREQUAL "")
    _no("cmake/GpuBackends.cmake missing")
else()
    set(_missing "")
    foreach(_sym ns_gpu_find_cuda_compiler ns_gpu_select_cuda_arch
                 ns_gpu_select_sycl_compiler NS_GPU_CUDA_ARCH_FALLBACK NS_GPU_SYCL_HINTS)
        if(NOT gpu MATCHES "${_sym}")
            list(APPEND _missing "${_sym}")
        endif()
    endforeach()
    if(_missing STREQUAL "")
        _ok("GpuBackends.cmake defines all 3 helpers + 2 constants")
    else()
        _no("GpuBackends.cmake missing: ${_missing}")
    endif()
endif()

# --- 4. optional-dependency converters skip gracefully ----------------------
_slurp(aom "${REPO}/src/ndmanager-plugins/src/process_aomconvert/CMakeLists.txt")
if(aom MATCHES "find_package\\(HDF5 QUIET" AND aom MATCHES "return\\(\\)")
    _ok("process_aomconvert skips gracefully when HDF5 absent")
else()
    _no("process_aomconvert no longer QUIET-probes/return()s on missing HDF5")
endif()
_slurp(leds "${REPO}/src/ndmanager-plugins/src/process_extractleds/CMakeLists.txt")
if(leds MATCHES "QUIET" AND leds MATCHES "return\\(\\)")
    _ok("process_extractleds skips gracefully when FFmpeg absent")
else()
    _no("process_extractleds no longer QUIET-probes/return()s on missing FFmpeg")
endif()

# --- 5. no hardcoded -march=native in the cleaned targets -------------------
# (the only permitted literal outside ZenOptimizations is the NS_NATIVE_ARCH-
#  gated CFLAGS fragment in process_resample)
set(_march_offenders "")
foreach(_p
        "src/ndmanager-plugins/src/process_aomconvert/CMakeLists.txt"
        "src/ndmanager-plugins/src/process_decomposecollisions/CMakeLists.txt"
        "src/ndmanager-plugins/src/process_extractemg/CMakeLists.txt"
        "src/kiloklustakwik/CMakeLists.txt")
    _slurp(_c "${REPO}/${_p}")
    if(_c MATCHES "march=native")
        list(APPEND _march_offenders "${_p}")
    endif()
endforeach()
if(_march_offenders STREQUAL "")
    _ok("no hardcoded -march=native in cleaned targets")
else()
    _no("hardcoded -march=native reintroduced in: ${_march_offenders}")
endif()
# resample must gate its libsamplerate CFLAGS on NS_NATIVE_ARCH
_slurp(rs "${REPO}/src/ndmanager-plugins/src/process_resample/CMakeLists.txt")
if(rs MATCHES "NS_NATIVE_ARCH" AND rs MATCHES "_lsr_march")
    _ok("process_resample gates libsamplerate -march=native on NS_NATIVE_ARCH")
else()
    _no("process_resample no longer gates libsamplerate CFLAGS on NS_NATIVE_ARCH")
endif()

# --- 6. cmake_minimum_required standardized to 3.22...3.31 ------------------
set(_minver_offenders "")
foreach(_root
        "CMakeLists.txt"
        "src/nphys-data/CMakeLists.txt"
        "src/libneurosuite-core/CMakeLists.txt"
        "src/libklustersshared/CMakeLists.txt"
        "src/klusters/CMakeLists.txt"
        "src/neuroscope/CMakeLists.txt"
        "src/ndmanager/CMakeLists.txt"
        "src/ndmanager-plugins/CMakeLists.txt"
        "src/kiloklustakwik/CMakeLists.txt")
    _slurp(_c "${REPO}/${_root}")
    if(NOT _c MATCHES "cmake_minimum_required\\(VERSION 3\\.22\\.\\.\\.3\\.31\\)")
        list(APPEND _minver_offenders "${_root}")
    endif()
endforeach()
if(_minver_offenders STREQUAL "")
    _ok("cmake_minimum_required is 3.22...3.31 across all roots")
else()
    _no("cmake_minimum_required not standardized in: ${_minver_offenders}")
endif()

# --- 7. nphys-data is data-only ---------------------------------------------
_slurp(nphys "${REPO}/src/nphys-data/CMakeLists.txt")
if(nphys MATCHES "LANGUAGES NONE")
    _ok("nphys-data declared LANGUAGES NONE")
else()
    _no("nphys-data no longer LANGUAGES NONE")
endif()

# --- 8. one C++ standard: C++20 (STANDARDIZATION.md §2.9) -------------------
# Every project root sets CMAKE_CXX_STANDARD 20, and no target asks for a
# different cxx_std_NN.  process_smrconvert is an external package (§1) and the
# vendored libsamplerate is never touched, so both are exempt.
set(_std_offenders "")
foreach(_root
        "CMakeLists.txt"
        "src/libneurosuite-core/CMakeLists.txt"
        "src/libklustersshared/CMakeLists.txt"
        "src/klusters/CMakeLists.txt"
        "src/neuroscope/CMakeLists.txt"
        "src/ndmanager/CMakeLists.txt"
        "src/ndmanager-plugins/CMakeLists.txt"
        "src/kiloklustakwik/CMakeLists.txt")
    _slurp(_c "${REPO}/${_root}")
    if(NOT _c MATCHES "set\\(CMAKE_CXX_STANDARD +20\\)")
        list(APPEND _std_offenders "${_root}")
    endif()
endforeach()
set(_feat_offenders "")
foreach(_f ${_files})
    if((_f MATCHES "CMakeLists\\.txt$" OR _f MATCHES "\\.cmake$")
            AND NOT _f MATCHES "libsamplerate" AND NOT _f MATCHES "process_smrconvert")
        file(READ "${_f}" _c)
        if(_c MATCHES "cxx_std_(98|11|14|17|23|26)" OR _c MATCHES "CXX_STANDARD +(98|11|14|17|23|26)")
            file(RELATIVE_PATH _rel "${REPO}" "${_f}")
            list(APPEND _feat_offenders "${_rel}")
        endif()
    endif()
endforeach()
if(_std_offenders STREQUAL "" AND _feat_offenders STREQUAL "")
    _ok("C++20 everywhere (CMAKE_CXX_STANDARD 20 at every root, no other cxx_std_NN)")
else()
    if(NOT _std_offenders STREQUAL "")
        _no("CMAKE_CXX_STANDARD 20 missing in: ${_std_offenders}")
    endif()
    if(NOT _feat_offenders STREQUAL "")
        _no("another C++ standard requested in: ${_feat_offenders}")
    endif()
endif()

# --- 9. standard subproject layout (STANDARDIZATION.md §2.7) ----------------
# Tests live in <sub>/test/ (never tests/), and no C/C++ source sits at a
# subproject root.  The top-level tests/ (this script) is not a subproject.
file(GLOB _subs LIST_DIRECTORIES true "${REPO}/src/*")
set(_layout_offenders "")
foreach(_s ${_subs})
    if(IS_DIRECTORY "${_s}")
        file(RELATIVE_PATH _rel "${REPO}" "${_s}")
        if(IS_DIRECTORY "${_s}/tests")
            list(APPEND _layout_offenders "${_rel}/tests (use test/)")
        endif()
        file(GLOB _loose LIST_DIRECTORIES false
             "${_s}/*.c" "${_s}/*.cc" "${_s}/*.cpp" "${_s}/*.cxx" "${_s}/*.h" "${_s}/*.hpp")
        foreach(_l ${_loose})
            file(RELATIVE_PATH _lr "${REPO}" "${_l}")
            list(APPEND _layout_offenders "${_lr}")
        endforeach()
    endif()
endforeach()
if(_layout_offenders STREQUAL "")
    _ok("subprojects keep tests in test/ and no sources at their root")
else()
    _no("subproject layout broken: ${_layout_offenders}")
endif()

# --- result -----------------------------------------------------------------
list(LENGTH _failed _n)
if(_n GREATER 0)
    message(FATAL_ERROR "${_n} build-system invariant(s) regressed")
endif()
message(STATUS "All build-system invariants hold.")
