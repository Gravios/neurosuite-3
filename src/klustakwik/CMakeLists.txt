cmake_minimum_required(VERSION 3.21)

# Project declared without GPU languages initially — each backend is probed
# and enabled only when its compiler/toolkit is actually present, so the
# build never fails on machines without a particular GPU toolkit.
project(KlustaKwik LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Backend options
#
#   USE_CUDA — NVIDIA GPUs via nvcc          (requires CUDA toolkit)
#   USE_HIP  — AMD GPUs via hipcc/ROCm       (requires ROCm >= 5.0)
#   USE_SYCL — Intel Arc/Xe via icpx/oneAPI  (requires oneAPI Base Toolkit >= 2023.1)
#
# All three default to ON so whichever toolkit is present is auto-selected.
# Exactly one backend may be active at a time; the priority order when
# multiple are found is: CUDA > HIP > SYCL.
# Force a specific backend: cmake -DUSE_CUDA=OFF -DUSE_HIP=OFF   (SYCL only)
#                           cmake -DUSE_CUDA=OFF -DUSE_SYCL=OFF  (HIP only)
#                           cmake -DUSE_HIP=OFF  -DUSE_SYCL=OFF  (CUDA only)
# =============================================================================
option(USE_CUDA "Build with NVIDIA CUDA GPU acceleration"        ON)
option(USE_HIP  "Build with AMD ROCm/HIP GPU acceleration"       ON)
option(USE_SYCL "Build with Intel SYCL/oneAPI GPU acceleration"  ON)

include(CheckLanguage)

# =============================================================================
# CUDA probe
# =============================================================================
set(_CUDA_AVAILABLE FALSE)
if(USE_CUDA)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        set(CMAKE_CUDA_ARCHITECTURES "86;89;100;120")
        set(_CUDA_AVAILABLE TRUE)
        message(STATUS "KlustaKwik: CUDA available  -- ${CMAKE_CUDA_COMPILER}")
    else()
        message(STATUS "KlustaKwik: CUDA requested but nvcc not found")
    endif()
endif()

# =============================================================================
# HIP probe (only if CUDA not already selected)
# =============================================================================
set(_HIP_AVAILABLE FALSE)
if(USE_HIP AND NOT _CUDA_AVAILABLE)
    find_program(_HIPCC hipcc
        HINTS
            $ENV{ROCM_PATH}/bin
            /opt/rocm/bin
            /usr/bin
    )
    if(_HIPCC)
        find_package(hip QUIET
            PATHS
                $ENV{ROCM_PATH}/lib/cmake/hip
                /opt/rocm/lib/cmake/hip
        )
        set(_HIP_AVAILABLE TRUE)
        message(STATUS "KlustaKwik: HIP available   -- ${_HIPCC}")
        if(hip_FOUND)
            message(STATUS "  ROCm CMake package version: ${hip_VERSION}")
        endif()
    else()
        message(STATUS "KlustaKwik: HIP requested but hipcc not found. "
                       "Install ROCm: https://rocm.docs.amd.com/en/latest/deploy/linux/index.html")
    endif()
endif()

# =============================================================================
# SYCL probe (only if neither CUDA nor HIP selected)
# =============================================================================
set(_SYCL_AVAILABLE FALSE)
if(USE_SYCL AND NOT _CUDA_AVAILABLE AND NOT _HIP_AVAILABLE)
    find_program(_ICPX icpx
        HINTS
            $ENV{ONEAPI_ROOT}/compiler/latest/bin
            /opt/intel/oneapi/compiler/latest/bin
    )
    if(_ICPX)
        set(_SYCL_AVAILABLE TRUE)
        message(STATUS "KlustaKwik: SYCL available  -- ${_ICPX}")
    else()
        message(STATUS "KlustaKwik: SYCL requested but icpx not found. "
                       "Install oneAPI: https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit.html")
    endif()
endif()

# =============================================================================
# Summary
# =============================================================================
if(_CUDA_AVAILABLE)
    message(STATUS "KlustaKwik: selected backend -> CUDA  (NVIDIA)")
elseif(_HIP_AVAILABLE)
    message(STATUS "KlustaKwik: selected backend -> HIP   (AMD ROCm)")
elseif(_SYCL_AVAILABLE)
    message(STATUS "KlustaKwik: selected backend -> SYCL  (Intel Arc/Xe)")
else()
    message(STATUS "KlustaKwik: selected backend -> CPU only (no GPU toolkit found)")
endif()

# =============================================================================
# Common sources
# =============================================================================
set(CPU_SOURCES KlustaKwik.cpp KK.cpp param.c)
set(HEADERS
    KlustaKwik.h KK.h
    KK_cuda.h KK_sycl.h KK_hip.h
    Array.h KlustaSave.h param.h
)

find_package(OpenMP)

set(_OPT_FLAGS $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -Wall -Wextra>)

# =============================================================================
# CPU-only fallback
# =============================================================================
if(NOT _CUDA_AVAILABLE AND NOT _HIP_AVAILABLE AND NOT _SYCL_AVAILABLE)
    add_executable(KlustaKwik ${CPU_SOURCES} ${HEADERS})
    target_compile_options(KlustaKwik PRIVATE ${_OPT_FLAGS})
    target_compile_definitions(KlustaKwik PRIVATE NDEBUG)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(KlustaKwik PRIVATE OpenMP::OpenMP_CXX)
    endif()
    install(TARGETS KlustaKwik DESTINATION bin)

    add_executable(KlustaKwik_cpu ${CPU_SOURCES} ${HEADERS})
    target_compile_options(KlustaKwik_cpu PRIVATE ${_OPT_FLAGS})
    target_compile_definitions(KlustaKwik_cpu PRIVATE NDEBUG)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(KlustaKwik_cpu PRIVATE OpenMP::OpenMP_CXX)
    endif()
    install(TARGETS KlustaKwik_cpu DESTINATION bin)
    return()
endif()

# =============================================================================
# CUDA build  (NVIDIA)
# =============================================================================
if(_CUDA_AVAILABLE)
    find_package(CUDAToolkit REQUIRED)

    add_executable(KlustaKwik ${CPU_SOURCES} KK_cuda.cu ${HEADERS})
    target_compile_options(KlustaKwik PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${_OPT_FLAGS}>
        $<$<COMPILE_LANGUAGE:CUDA>:
            -O3 --use_fast_math -Xptxas -v --extended-lambda
        >
    )
    target_compile_definitions(KlustaKwik PRIVATE NDEBUG USE_CUDA)
    target_link_libraries(KlustaKwik PRIVATE
        CUDA::cudart m
        $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
    )
    message(STATUS "  CUDA architectures : ${CMAKE_CUDA_ARCHITECTURES}")
    message(STATUS "  CUDA toolkit       : ${CUDAToolkit_VERSION}")
endif()

# =============================================================================
# HIP build  (AMD ROCm)
# =============================================================================
if(_HIP_AVAILABLE)
    # GPU architecture targets.
    # RDNA2: gfx1030/1031/1032 (RX 6000 series)
    # RDNA3: gfx1100/1101/1102 (RX 7000 series)
    # CDNA1: gfx908             (Instinct MI100)
    # CDNA2: gfx90a             (Instinct MI210/MI250)
    # Override: cmake -DKK_HIP_ARCHS="gfx1100;gfx90a"
    if(NOT DEFINED KK_HIP_ARCHS)
        set(KK_HIP_ARCHS "gfx1030;gfx1031;gfx1100;gfx1101;gfx90a;gfx908")
    endif()
    string(REPLACE ";" "," _HIP_ARCH_COMMA "${KK_HIP_ARCHS}")

    # KK_hip.cpp contains __global__ kernels — must be compiled by hipcc.
    # We build it as a separate OBJECT library with hipcc as the CXX compiler,
    # then link it into the main executable which is compiled with the system CXX.
    # This avoids changing CMAKE_CXX_COMPILER globally.
    add_library(kk_hip_obj OBJECT KK_hip.cpp)
    set_property(TARGET kk_hip_obj PROPERTY CXX_COMPILER "${_HIPCC}")
    set_target_properties(kk_hip_obj PROPERTIES CXX_COMPILER_LAUNCHER "")
    target_compile_options(kk_hip_obj PRIVATE
        -O3 -march=native
        --offload-arch=${_HIP_ARCH_COMMA}
        -fgpu-rdc
        -Wall -Wextra
        # Raise wavefront block size for CDNA2 (MI210/MI250 have 64 CUs):
        # -DKK_HIP_BLOCK=512
    )
    target_compile_definitions(kk_hip_obj PRIVATE NDEBUG USE_HIP)
    target_include_directories(kk_hip_obj PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        $ENV{ROCM_PATH}/include
        /opt/rocm/include
    )

    add_executable(KlustaKwik ${CPU_SOURCES} ${HEADERS})
    target_compile_options(KlustaKwik PRIVATE ${_OPT_FLAGS})
    target_compile_definitions(KlustaKwik PRIVATE NDEBUG USE_HIP)
    target_include_directories(KlustaKwik PRIVATE
        $ENV{ROCM_PATH}/include /opt/rocm/include
    )
    target_link_libraries(KlustaKwik PRIVATE
        kk_hip_obj
        $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
        -L$ENV{ROCM_PATH}/lib -L/opt/rocm/lib -lamdhip64
    )
    # hipcc must drive the final link when -fgpu-rdc is used
    target_link_options(KlustaKwik PRIVATE
        -fgpu-rdc --offload-arch=${_HIP_ARCH_COMMA}
    )
    set_target_properties(KlustaKwik PROPERTIES LINKER "${_HIPCC}")

    message(STATUS "  HIP architectures  : ${KK_HIP_ARCHS}")
endif()

# =============================================================================
# SYCL build  (Intel Arc/Xe)
# =============================================================================
if(_SYCL_AVAILABLE)
    add_executable(KlustaKwik_sycl ${CPU_SOURCES} KK_sycl.cpp ${HEADERS})
    target_compile_options(KlustaKwik_sycl PRIVATE
        -O3 -march=native
        -fsycl
        -fsycl-targets=spir64_gen
        -Xs "-device pvc,acm-g10,acm-g11,dg2"
        -Wall -Wextra
    )
    target_compile_definitions(KlustaKwik_sycl PRIVATE NDEBUG USE_SYCL)
    target_link_options(KlustaKwik_sycl PRIVATE
        -fsycl
        -fsycl-targets=spir64_gen
        -Xs "-device pvc,acm-g10,acm-g11,dg2"
    )
    target_link_libraries(KlustaKwik_sycl PRIVATE
        $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
    )
    set_target_properties(KlustaKwik_sycl PROPERTIES OUTPUT_NAME KlustaKwik_sycl)
    install(TARGETS KlustaKwik_sycl DESTINATION bin)
    # Also install as the canonical name so ndmanager can call it without
    # knowing which backend was built.
    install(PROGRAMS $<TARGET_FILE:KlustaKwik_sycl>
            DESTINATION bin RENAME KlustaKwik)

    message(STATUS "  SYCL compiler      : ${_ICPX}")
    message(STATUS "  SYCL targets       : spir64_gen (Arc/Xe/PVC)")
endif()

# =============================================================================
# CPU reference binary — always built alongside any GPU binary
# =============================================================================
add_executable(KlustaKwik_cpu ${CPU_SOURCES} ${HEADERS})
target_compile_options(KlustaKwik_cpu PRIVATE ${_OPT_FLAGS})
target_compile_definitions(KlustaKwik_cpu PRIVATE NDEBUG)
if(OpenMP_CXX_FOUND)
    target_link_libraries(KlustaKwik_cpu PRIVATE OpenMP::OpenMP_CXX)
endif()
install(TARGETS KlustaKwik_cpu DESTINATION bin)

if(_CUDA_AVAILABLE OR _HIP_AVAILABLE)
    install(TARGETS KlustaKwik DESTINATION bin)
endif()
