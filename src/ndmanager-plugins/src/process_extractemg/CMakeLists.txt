# =============================================================================
# process_extractemg — FastICA-based EMG identification, removal, and
# event extraction.
#
# CPU code path is always built.  Optional GPU backends accelerate the
# FastICA pow3 inner kernel:
#
#   USE_CUDA — NVIDIA GPUs via nvcc        (requires CUDA Toolkit)
#   USE_HIP  — AMD GPUs via hipcc/ROCm     (requires ROCm + rocBLAS)
#   USE_SYCL — Intel Arc/Xe via icpx       (requires oneAPI + oneMKL)
#
# All three default ON; whichever toolkit is present is auto-selected.
# Priority when multiple are found: CUDA > HIP > SYCL > CPU.  This
# matches the convention used by KlustaKwik (src/klustakwik/CMakeLists.txt
# lines 87-89) and the top-level neurosuite-3 build.
#
# Force a specific backend by turning the others off:
#   CUDA only:  cmake -DUSE_HIP=OFF  -DUSE_SYCL=OFF
#   HIP  only:  cmake -DUSE_CUDA=OFF -DUSE_SYCL=OFF
#   SYCL only:  cmake -DUSE_CUDA=OFF -DUSE_HIP=OFF
#   CPU only:   cmake -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF
# =============================================================================
cmake_minimum_required(VERSION 3.16)

option(USE_CUDA "Build with NVIDIA CUDA GPU acceleration"       ON)
option(USE_HIP  "Build with AMD ROCm/HIP GPU acceleration"      ON)
option(USE_SYCL "Build with Intel SYCL/oneAPI GPU acceleration" ON)

include(CheckLanguage)

# =============================================================================
# CUDA probe
# =============================================================================
set(_emg_cuda_active FALSE)
if(USE_CUDA)
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

# =============================================================================
# HIP probe (skipped when CUDA already selected)
# =============================================================================
set(_emg_hip_active FALSE)
if(USE_HIP AND NOT _emg_cuda_active)
    find_program(_HIPCC hipcc HINTS $ENV{ROCM_PATH}/bin /opt/rocm/bin /usr/bin)
    if(_HIPCC)
        find_package(hip      QUIET PATHS $ENV{ROCM_PATH}/lib/cmake/hip
                                          /opt/rocm/lib/cmake/hip)
        find_package(rocblas  QUIET PATHS $ENV{ROCM_PATH}/lib/cmake/rocblas
                                          /opt/rocm/lib/cmake/rocblas)
        set(_emg_hip_active TRUE)
        message(STATUS "process_extractemg: HIP available — ${_HIPCC}")
    else()
        message(STATUS
            "process_extractemg: HIP requested but hipcc not found — "
            "install ROCm: https://rocm.docs.amd.com/")
    endif()
endif()

# =============================================================================
# SYCL probe (skipped when CUDA or HIP already selected)
# =============================================================================
set(_emg_sycl_active FALSE)
if(USE_SYCL AND NOT _emg_cuda_active AND NOT _emg_hip_active)
    find_program(_ICPX icpx
        HINTS $ENV{ONEAPI_ROOT}/compiler/latest/bin
              /opt/intel/oneapi/compiler/latest/bin
        NO_DEFAULT_PATH)
    if(_ICPX)
        execute_process(
            COMMAND "${_ICPX}" --version
            OUTPUT_VARIABLE _ICPX_VER
            ERROR_VARIABLE  _ICPX_VER
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_ICPX_VER MATCHES "Intel")
            set(_emg_sycl_active TRUE)
            message(STATUS "process_extractemg: SYCL available — ${_ICPX}")
        else()
            message(STATUS
                "process_extractemg: icpx found but does not identify as Intel — "
                "skipping SYCL build")
            unset(_ICPX CACHE)
        endif()
    else()
        message(STATUS
            "process_extractemg: SYCL requested but icpx not found — "
            "install oneAPI: https://www.intel.com/content/www/us/en/developer/"
            "tools/oneapi/base-toolkit.html")
    endif()
endif()

# =============================================================================
# Backend summary
# =============================================================================
if(_emg_cuda_active)
    message(STATUS "process_extractemg: active backend → CUDA  (NVIDIA)")
elseif(_emg_hip_active)
    message(STATUS "process_extractemg: active backend → HIP   (AMD ROCm)")
elseif(_emg_sycl_active)
    message(STATUS "process_extractemg: active backend → SYCL  (Intel Arc/Xe)")
else()
    message(STATUS "process_extractemg: active backend → CPU only (no GPU toolkit found)")
endif()

# =============================================================================
# Source list
# =============================================================================
set(_emg_srcs
    process_extractemg.cpp
    process_extractemg_gpu_cpu.cpp
)
if(_emg_cuda_active)
    list(APPEND _emg_srcs process_extractemg_cuda.cu)
endif()
# HIP / SYCL sources are compiled by hipcc / icpx via OBJECT libraries
# below — same pattern as KlustaKwik's KK_hip.cpp / KK_sycl.cpp.

add_executable(process_extractemg ${_emg_srcs})
target_compile_features(process_extractemg PRIVATE cxx_std_17)
target_compile_definitions(process_extractemg PRIVATE _USE_MATH_DEFINES)

# =============================================================================
# CPU OpenMP — used by the multi-probe parallel path and the pow3 inner
# OpenMP fallback when no GPU backend is active.
# =============================================================================
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_extractemg PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_extractemg: OpenMP enabled")
else()
    message(STATUS "process_extractemg: OpenMP not found, single-threaded CPU path")
endif()

# =============================================================================
# CUDA wiring (NVIDIA)
# =============================================================================
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

# =============================================================================
# HIP wiring (AMD ROCm) — OBJECT lib compiled by hipcc, linked into the
# main executable compiled with the system CXX.  Mirrors KlustaKwik's
# kk_hip_kernels pattern; avoids changing CMAKE_CXX_COMPILER globally.
# =============================================================================
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

# =============================================================================
# SYCL wiring (Intel oneAPI) — OBJECT lib compiled by icpx, oneMKL gemv.
# =============================================================================
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

install(TARGETS process_extractemg
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

add_manpage(process_extractemg)
