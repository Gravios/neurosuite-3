# =============================================================================
# GpuBackends.cmake — shared GPU-backend detection helpers
#
# Consolidates the CUDA / SYCL probe logic that the monorepo top-level and the
# kiloklustakwik subproject previously each carried their own copy of: the
# nvcc location search, the CUDA architecture fallback list, the oneAPI/icpx
# search hints, and the pre-project() icpx compiler auto-selection.
#
# These are HELPERS — including the module defines functions and two shared
# constants but probes nothing on its own.  Each consumer keeps its own control
# flow and calls the helpers it needs; the behaviour of each helper is identical
# to the inlined code it replaces.
#
# All three GPU-aware build entry points use this module: the monorepo top-level
# and the kiloklustakwik and klusters subprojects.  Each is built within the
# repo, so the repo-relative include path the subprojects use is always valid.
# =============================================================================
include_guard(GLOBAL)

# Canonical CUDA architecture list — Turing through Blackwell (sm_75..sm_120) —
# used when "native" auto-detection is unavailable (no GPU at configure time, or
# CMake < 3.24).  Single source of truth; user-overridable.
set(NS_GPU_CUDA_ARCH_FALLBACK "75;86;89;100;120"
    CACHE STRING "CUDA architectures to target when 'native' auto-detection is unavailable")

# Canonical oneAPI / icpx search hints (the union of the locations the consumers
# searched).  Used by both the pre-project() compiler selection and any
# post-project() SYCL probe.
set(NS_GPU_SYCL_HINTS
    "$ENV{CMPLR_ROOT}/bin"
    "$ENV{ONEAPI_ROOT}/compiler/latest/bin"
    "$ENV{ONEAPI_ROOT}/compiler/latest/linux/bin"
    "/opt/intel/oneapi/compiler/latest/bin"
    "/opt/intel/oneapi/compiler/latest/linux/bin"
    CACHE INTERNAL "oneAPI/icpx search hints")

# -----------------------------------------------------------------------------
# ns_gpu_find_cuda_compiler()
#   Locate nvcc and set CMAKE_CUDA_COMPILER in the cache, if not already set.
#   Searches PATH first, then the platform's conventional toolkit locations.
#   Pre-project() safe (only sets a cache variable).  No-op if the user already
#   passed -DCMAKE_CUDA_COMPILER=.
# -----------------------------------------------------------------------------
function(ns_gpu_find_cuda_compiler)
    if(CMAKE_CUDA_COMPILER)
        return()
    endif()
    # find_program searches PATH (unlike check_language which uses hard-coded
    # locations).  Run it first so an nvcc that is on PATH is always found.
    find_program(_nvcc_on_path nvcc)
    if(_nvcc_on_path)
        set(CMAKE_CUDA_COMPILER "${_nvcc_on_path}"
            CACHE FILEPATH "CUDA compiler" FORCE)
    else()
        # Fallback: common fixed installation roots, platform-specific.
        if(WIN32)
            file(GLOB _cuda_roots
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/*/bin/nvcc.exe"
                "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v*/bin/nvcc.exe")
            foreach(_env_root "$ENV{CUDA_PATH}" "$ENV{CUDA_HOME}" "$ENV{CUDA_PATH_V12_0}")
                if(EXISTS "${_env_root}/bin/nvcc.exe")
                    list(PREPEND _cuda_roots "${_env_root}/bin/nvcc.exe")
                endif()
            endforeach()
            if(_cuda_roots)
                list(SORT _cuda_roots ORDER DESCENDING)
                list(GET _cuda_roots 0 _first_nvcc)
                set(CMAKE_CUDA_COMPILER "${_first_nvcc}"
                    CACHE FILEPATH "CUDA compiler" FORCE)
            endif()
        elseif(APPLE)
            # CUDA toolkit support for macOS was dropped after 10.2.  Skip.
            message(STATUS "neurosuite-3: CUDA not supported on macOS — skipping")
        else()
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
endfunction()

# -----------------------------------------------------------------------------
# ns_gpu_select_cuda_arch()
#   Choose CMAKE_CUDA_ARCHITECTURES if not already set: "native" when a GPU is
#   visible (nvidia-smi) and CMake >= 3.24, otherwise NS_GPU_CUDA_ARCH_FALLBACK.
#   Must run BEFORE project() so the value is in the cache when the CUDA language
#   is initialised.  Call only when CUDA is actually being enabled.
# -----------------------------------------------------------------------------
function(ns_gpu_select_cuda_arch)
    if(CMAKE_CUDA_ARCHITECTURES)
        return()
    endif()
    find_program(_nvidia_smi nvidia-smi)
    if(_nvidia_smi)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
            set(CMAKE_CUDA_ARCHITECTURES "native"
                CACHE STRING "CUDA architectures (native = auto-detect installed GPU)" FORCE)
            message(STATUS "neurosuite-3: CUDA architectures = native (GPU detected via nvidia-smi)")
        else()
            set(CMAKE_CUDA_ARCHITECTURES "${NS_GPU_CUDA_ARCH_FALLBACK}"
                CACHE STRING "CUDA architectures (CMake < 3.24, native not supported)" FORCE)
            message(STATUS "neurosuite-3: CUDA architectures = ${NS_GPU_CUDA_ARCH_FALLBACK} (CMake < 3.24)")
        endif()
    else()
        # No GPU at configure time (headless/CI) — explicit list.
        set(CMAKE_CUDA_ARCHITECTURES "${NS_GPU_CUDA_ARCH_FALLBACK}"
            CACHE STRING "CUDA architectures (explicit list — no GPU detected)" FORCE)
        message(STATUS "neurosuite-3: CUDA architectures = ${NS_GPU_CUDA_ARCH_FALLBACK} (no GPU at configure time)")
    endif()
    unset(_nvidia_smi CACHE)
endfunction()

# -----------------------------------------------------------------------------
# ns_gpu_select_sycl_compiler()
#   Pre-project(): auto-select icpx/icx as the C++/C compiler for SYCL, but only
#   when SYCL is not explicitly disabled AND the caller has not already chosen a
#   compiler (-DCMAKE_CXX_COMPILER= or the CXX env var).  CMake locks the
#   compiler at project() time, so this MUST be called before project().
#   Only switches if icpx is found in NS_GPU_SYCL_HINTS and identifies as Intel.
# -----------------------------------------------------------------------------
function(ns_gpu_select_sycl_compiler)
    if(DEFINED USE_SYCL AND NOT USE_SYCL)
        return()
    endif()
    if(DEFINED CMAKE_CXX_COMPILER OR DEFINED ENV{CXX})
        return()
    endif()
    find_program(_ns_icpx_early icpx HINTS ${NS_GPU_SYCL_HINTS} NO_DEFAULT_PATH)
    if(NOT _ns_icpx_early)
        unset(_ns_icpx_early CACHE)
        return()
    endif()
    execute_process(
        COMMAND "${_ns_icpx_early}" --version
        OUTPUT_VARIABLE _ns_icpx_ver
        ERROR_VARIABLE  _ns_icpx_ver
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_ns_icpx_ver MATCHES "Intel")
        get_filename_component(_ns_icpx_dir "${_ns_icpx_early}" DIRECTORY)
        set(CMAKE_CXX_COMPILER "${_ns_icpx_early}" CACHE FILEPATH
            "C++ compiler — auto-selected icpx for SYCL" FORCE)
        set(CMAKE_C_COMPILER "${_ns_icpx_dir}/icx" CACHE FILEPATH
            "C compiler — auto-selected icx for SYCL" FORCE)
        message(STATUS
            "neurosuite-3: auto-selected icpx for SYCL (override: -DCMAKE_CXX_COMPILER=)")
    endif()
    unset(_ns_icpx_early CACHE)
endfunction()
