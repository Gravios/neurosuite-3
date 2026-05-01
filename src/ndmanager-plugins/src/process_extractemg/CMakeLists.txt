# =============================================================================
# process_extractemg — FastICA-based EMG identification, removal, and
# event extraction.
#
# Build configuration
# -------------------
# This plugin compiles the same CPU / OpenMP code path on every platform.
# Optional GPU backends for the FastICA pow3 inner kernel are enabled at
# configure time via:
#
#     -DEXTRACTEMG_USE_CUDA=ON   (NVIDIA, requires CUDA Toolkit)
#     -DEXTRACTEMG_USE_HIP=ON    (AMD ROCm,  requires hipcc + rocBLAS)
#     -DEXTRACTEMG_USE_SYCL=ON   (Intel oneAPI, requires icpx + oneMKL)
#
# Priority when more than one is found at runtime: CUDA > HIP > SYCL > CPU
# (matches KlustaKwik's convention).  Build-time defaults are OFF for
# all three so the plugin always builds even on a CPU-only host.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

# -----------------------------------------------------------------------------
# Backend toggles
#
# We use a namespaced option (EXTRACTEMG_USE_*) so the plugin can be
# configured independently of other backends in the tree.  The default
# value mirrors the top-level USE_CUDA / USE_HIP / USE_SYCL flag when
# the plugin is built as part of the neurosuite-3 monorepo, so a user
# who passes -DUSE_CUDA=ON to the top-level cmake gets GPU here too;
# OFF when the plugin is built standalone (USE_* not defined).  Pass
# -DEXTRACTEMG_USE_CUDA=OFF to force CPU-only when the rest of the
# tree builds with CUDA.
# -----------------------------------------------------------------------------
option(EXTRACTEMG_USE_CUDA "Build process_extractemg with NVIDIA CUDA pow3 kernel"
       "${USE_CUDA}")
option(EXTRACTEMG_USE_HIP  "Build process_extractemg with AMD ROCm/HIP pow3 kernel"
       "${USE_HIP}")
option(EXTRACTEMG_USE_SYCL "Build process_extractemg with Intel SYCL/oneAPI pow3 kernel"
       "${USE_SYCL}")

include(CheckLanguage)

# -----------------------------------------------------------------------------
# CUDA probe
# -----------------------------------------------------------------------------
set(_emg_cuda_active FALSE)
if(EXTRACTEMG_USE_CUDA)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD          17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
            set(CMAKE_CUDA_ARCHITECTURES "86;89;100;120"
                CACHE STRING "CUDA architectures to target")
        endif()
        set(_emg_cuda_active TRUE)
        message(STATUS "process_extractemg: CUDA available — ${CMAKE_CUDA_COMPILER}")
    else()
        message(STATUS "process_extractemg: CUDA requested but nvcc not found")
    endif()
endif()

# -----------------------------------------------------------------------------
# HIP probe (skipped when CUDA already selected)
# -----------------------------------------------------------------------------
set(_emg_hip_active FALSE)
if(EXTRACTEMG_USE_HIP AND NOT _emg_cuda_active)
    find_program(_HIPCC hipcc HINTS $ENV{ROCM_PATH}/bin /opt/rocm/bin /usr/bin)
    if(_HIPCC)
        find_package(hip      QUIET PATHS $ENV{ROCM_PATH}/lib/cmake/hip
                                        /opt/rocm/lib/cmake/hip)
        find_package(rocblas  QUIET PATHS $ENV{ROCM_PATH}/lib/cmake/rocblas
                                        /opt/rocm/lib/cmake/rocblas)
        set(_emg_hip_active TRUE)
        message(STATUS "process_extractemg: HIP available — ${_HIPCC}")
    else()
        message(STATUS "process_extractemg: HIP requested but hipcc not found")
    endif()
endif()

# -----------------------------------------------------------------------------
# SYCL probe (skipped when CUDA or HIP already selected)
# -----------------------------------------------------------------------------
set(_emg_sycl_active FALSE)
if(EXTRACTEMG_USE_SYCL AND NOT _emg_cuda_active AND NOT _emg_hip_active)
    find_program(_ICPX icpx
        HINTS $ENV{ONEAPI_ROOT}/compiler/latest/bin
              /opt/intel/oneapi/compiler/latest/bin
        NO_DEFAULT_PATH)
    if(_ICPX)
        set(_emg_sycl_active TRUE)
        message(STATUS "process_extractemg: SYCL available — ${_ICPX}")
    else()
        message(STATUS "process_extractemg: SYCL requested but icpx not found")
    endif()
endif()

# -----------------------------------------------------------------------------
# Source list
# -----------------------------------------------------------------------------
set(_emg_srcs
    process_extractemg.cpp
    process_extractemg_gpu_cpu.cpp
)

if(_emg_cuda_active)
    list(APPEND _emg_srcs process_extractemg_cuda.cu)
endif()
# HIP and SYCL sources go through OBJECT libs below — see KlustaKwik
# for the pattern: hipcc / icpx must compile their own translation
# units, but the rest of the executable is plain g++ / clang.

add_executable(process_extractemg ${_emg_srcs})
target_compile_features(process_extractemg PRIVATE cxx_std_17)

target_compile_definitions(process_extractemg PRIVATE _USE_MATH_DEFINES)

# -----------------------------------------------------------------------------
# CPU OpenMP — used by the multi-probe parallel and pow3 inner OMP path.
# -----------------------------------------------------------------------------
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_extractemg PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_extractemg: OpenMP enabled")
else()
    message(STATUS "process_extractemg: OpenMP not found, single-threaded CPU path")
endif()

# -----------------------------------------------------------------------------
# CUDA wiring
# -----------------------------------------------------------------------------
if(_emg_cuda_active)
    find_package(CUDAToolkit REQUIRED)
    target_compile_definitions(process_extractemg PRIVATE USE_CUDA)
    target_link_libraries(process_extractemg PRIVATE
        CUDA::cudart
        CUDA::cublas)
    target_compile_options(process_extractemg PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>: -O3 --use_fast_math --extended-lambda>)
    message(STATUS "  CUDA architectures : ${CMAKE_CUDA_ARCHITECTURES}")
endif()

# -----------------------------------------------------------------------------
# HIP wiring (OBJECT lib compiled by hipcc, then linked into the main exe)
# -----------------------------------------------------------------------------
if(_emg_hip_active)
    set(EXTRACTEMG_HIP_ARCHS "gfx1030;gfx1100;gfx90a;gfx908"
        CACHE STRING "HIP GPU architectures for process_extractemg")
    string(REPLACE ";" "," _hip_archs_comma "${EXTRACTEMG_HIP_ARCHS}")

    add_library(extractemg_hip_kernels OBJECT process_extractemg_hip.cpp)
    set_property(TARGET extractemg_hip_kernels PROPERTY CXX_COMPILER "${_HIPCC}")
    set_target_properties(extractemg_hip_kernels PROPERTIES CXX_COMPILER_LAUNCHER "")
    target_compile_options(extractemg_hip_kernels PRIVATE
        -O3 -march=native
        --offload-arch=${_hip_archs_comma}
        -fgpu-rdc -Wall -Wextra)
    target_compile_definitions(extractemg_hip_kernels PRIVATE USE_HIP)
    target_include_directories(extractemg_hip_kernels PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        $<$<BOOL:$ENV{ROCM_PATH}>:$ENV{ROCM_PATH}/include>
        /opt/rocm/include)

    target_compile_definitions(process_extractemg PRIVATE USE_HIP)
    target_link_libraries(process_extractemg PRIVATE extractemg_hip_kernels)
    if(hip_FOUND)
        target_link_libraries(process_extractemg PRIVATE hip::host)
    endif()
    if(rocblas_FOUND)
        target_link_libraries(process_extractemg PRIVATE roc::rocblas)
    endif()
endif()

# -----------------------------------------------------------------------------
# SYCL wiring (OBJECT lib compiled by icpx, oneMKL gemv)
# -----------------------------------------------------------------------------
if(_emg_sycl_active)
    add_library(extractemg_sycl_kernels OBJECT process_extractemg_sycl.cpp)
    set_property(TARGET extractemg_sycl_kernels PROPERTY CXX_COMPILER "${_ICPX}")
    set_target_properties(extractemg_sycl_kernels PROPERTIES CXX_COMPILER_LAUNCHER "")
    target_compile_options(extractemg_sycl_kernels PRIVATE
        -O3 -fsycl -fsycl-targets=spir64 -Wall -Wextra)
    target_compile_definitions(extractemg_sycl_kernels PRIVATE USE_SYCL)
    target_include_directories(extractemg_sycl_kernels PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        $<$<BOOL:$ENV{ONEAPI_ROOT}>:$ENV{ONEAPI_ROOT}/mkl/latest/include>)

    target_compile_definitions(process_extractemg PRIVATE USE_SYCL)
    target_link_libraries(process_extractemg PRIVATE extractemg_sycl_kernels)
    target_link_options(process_extractemg PRIVATE
        -fsycl -fsycl-targets=spir64
        -lmkl_sycl -lmkl_intel_ilp64 -lmkl_sequential -lmkl_core)
endif()

# -----------------------------------------------------------------------------
# Backend summary
# -----------------------------------------------------------------------------
if(_emg_cuda_active)
    message(STATUS "process_extractemg: pow3 GPU backend → CUDA")
elseif(_emg_hip_active)
    message(STATUS "process_extractemg: pow3 GPU backend → HIP")
elseif(_emg_sycl_active)
    message(STATUS "process_extractemg: pow3 GPU backend → SYCL")
else()
    message(STATUS "process_extractemg: pow3 GPU backend → none (CPU only)")
endif()

install(TARGETS process_extractemg
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_manpage(process_extractemg)
