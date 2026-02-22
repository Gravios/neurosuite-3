cmake_minimum_required(VERSION 3.21)

# =============================================================================
# SYCL compiler auto-selection (before project())
# =============================================================================
if(NOT DEFINED USE_SYCL OR NOT USE_SYCL STREQUAL "OFF")
    if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
        find_program(_ICPX_EARLY icpx
            HINTS
                $ENV{ONEAPI_ROOT}/compiler/latest/bin
                /opt/intel/oneapi/compiler/latest/bin
            NO_DEFAULT_PATH
        )
        if(_ICPX_EARLY)
            execute_process(
                COMMAND "${_ICPX_EARLY}" --version
                OUTPUT_VARIABLE _ICPX_EARLY_VER
                ERROR_VARIABLE  _ICPX_EARLY_VER
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(_ICPX_EARLY_VER MATCHES "Intel")
                get_filename_component(_ICPX_EARLY_DIR "${_ICPX_EARLY}" DIRECTORY)
                set(CMAKE_CXX_COMPILER "${_ICPX_EARLY}" CACHE FILEPATH
                    "C++ compiler — auto-selected icpx for SYCL support" FORCE)
                set(CMAKE_C_COMPILER "${_ICPX_EARLY_DIR}/icx" CACHE FILEPATH
                    "C compiler — auto-selected icx for SYCL support" FORCE)
                message(STATUS "SpikeRealign: auto-selected icpx for SYCL")
            endif()
        endif()
    endif()
endif()

project(SpikeRealign LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Backend options
# =============================================================================
option(USE_CUDA "Build with NVIDIA CUDA GPU acceleration"       ON)
option(USE_HIP  "Build with AMD ROCm/HIP GPU acceleration"      ON)
option(USE_SYCL "Build with Intel SYCL/oneAPI GPU acceleration" ON)

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
        set(CMAKE_CUDA_ARCHITECTURES "75;86;89;120")
        set(_CUDA_AVAILABLE TRUE)
        message(STATUS "SpikeRealign: CUDA available  -- ${CMAKE_CUDA_COMPILER}")
    else()
        message(STATUS "SpikeRealign: CUDA requested but nvcc not found")
    endif()
endif()

# =============================================================================
# HIP probe
# =============================================================================
set(_HIP_AVAILABLE FALSE)
if(USE_HIP AND NOT _CUDA_AVAILABLE)
    find_program(_HIPCC hipcc
        HINTS $ENV{ROCM_PATH}/bin /opt/rocm/bin /usr/bin
    )
    if(_HIPCC)
        find_package(hip QUIET
            PATHS $ENV{ROCM_PATH}/lib/cmake/hip /opt/rocm/lib/cmake/hip
        )
        set(_HIP_AVAILABLE TRUE)
        message(STATUS "SpikeRealign: HIP available   -- ${_HIPCC}")
    else()
        message(STATUS "SpikeRealign: HIP requested but hipcc not found")
    endif()
endif()

# =============================================================================
# SYCL probe
# =============================================================================
set(_SYCL_AVAILABLE FALSE)
if(USE_SYCL AND NOT _CUDA_AVAILABLE AND NOT _HIP_AVAILABLE)
    find_program(_ICPX icpx
        HINTS
            $ENV{ONEAPI_ROOT}/compiler/latest/bin
            /opt/intel/oneapi/compiler/latest/bin
        NO_DEFAULT_PATH
    )
    if(_ICPX)
        execute_process(
            COMMAND "${_ICPX}" --version
            OUTPUT_VARIABLE _ICPX_VERSION_OUT
            ERROR_VARIABLE  _ICPX_VERSION_OUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_ICPX_VERSION_OUT MATCHES "Intel")
            set(_SYCL_AVAILABLE TRUE)
            message(STATUS "SpikeRealign: SYCL available  -- ${_ICPX}")
        else()
            message(STATUS "SpikeRealign: icpx found but not Intel compiler — skipping SYCL")
            unset(_ICPX CACHE)
        endif()
    else()
        message(STATUS "SpikeRealign: SYCL requested but icpx not found")
    endif()
endif()

# =============================================================================
# Summary
# =============================================================================
if(_CUDA_AVAILABLE)
    message(STATUS "SpikeRealign: selected backend -> CUDA  (NVIDIA)")
elseif(_HIP_AVAILABLE)
    message(STATUS "SpikeRealign: selected backend -> HIP   (AMD ROCm)")
elseif(_SYCL_AVAILABLE)
    message(STATUS "SpikeRealign: selected backend -> SYCL  (Intel Arc/Xe)")
else()
    message(STATUS "SpikeRealign: selected backend -> CPU only")
endif()

find_package(OpenMP)

set(_OPT_FLAGS $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -Wall -Wextra>)

# Common CPU sources
set(CPU_SOURCES
    SpikeRealign.cpp
    realign_xcorr_omp.cpp
    realign_xcorr_dispatch.cpp
)

# =============================================================================
# CPU-only build
# =============================================================================
if(NOT _CUDA_AVAILABLE AND NOT _HIP_AVAILABLE AND NOT _SYCL_AVAILABLE)
    add_executable(SpikeRealign ${CPU_SOURCES})
    target_compile_options(SpikeRealign PRIVATE ${_OPT_FLAGS})
    target_compile_definitions(SpikeRealign PRIVATE NDEBUG)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(SpikeRealign PRIVATE OpenMP::OpenMP_CXX)
    endif()
    install(TARGETS SpikeRealign DESTINATION bin)

    add_executable(SpikeRealign_cpu ${CPU_SOURCES})
    target_compile_options(SpikeRealign_cpu PRIVATE ${_OPT_FLAGS})
    target_compile_definitions(SpikeRealign_cpu PRIVATE NDEBUG)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(SpikeRealign_cpu PRIVATE OpenMP::OpenMP_CXX)
    endif()
    install(TARGETS SpikeRealign_cpu DESTINATION bin)
    return()
endif()

# =============================================================================
# CUDA build
# =============================================================================
if(_CUDA_AVAILABLE)
    find_package(CUDAToolkit REQUIRED)

    add_executable(SpikeRealign
        ${CPU_SOURCES}
        realign_xcorr_cuda.cu
    )
    target_compile_options(SpikeRealign PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${_OPT_FLAGS}>
        $<$<COMPILE_LANGUAGE:CUDA>:
            -O3 --use_fast_math -Xptxas -v --extended-lambda
        >
    )
    target_compile_definitions(SpikeRealign PRIVATE NDEBUG USE_CUDA)
    target_link_libraries(SpikeRealign PRIVATE
        CUDA::cudart
        $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
    )
    install(TARGETS SpikeRealign DESTINATION bin)
endif()

# =============================================================================
# HIP build
# =============================================================================
if(_HIP_AVAILABLE)
    if(NOT DEFINED SR_HIP_ARCHS)
        set(SR_HIP_ARCHS "gfx1030;gfx1031;gfx1100;gfx1101;gfx90a;gfx908")
    endif()
    string(REPLACE ";" "," _HIP_ARCH_COMMA "${SR_HIP_ARCHS}")

    add_library(sr_hip_obj OBJECT realign_xcorr_hip.hip)
    set_property(TARGET sr_hip_obj PROPERTY CXX_COMPILER "${_HIPCC}")
    set_target_properties(sr_hip_obj PROPERTIES CXX_COMPILER_LAUNCHER "")
    target_compile_options(sr_hip_obj PRIVATE
        -O3 -march=native
        --offload-arch=${_HIP_ARCH_COMMA}
        -fgpu-rdc
        -Wall -Wextra
    )
    target_compile_definitions(sr_hip_obj PRIVATE NDEBUG USE_HIP)
    target_include_directories(sr_hip_obj PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        $ENV{ROCM_PATH}/include
        /opt/rocm/include
    )

    add_executable(SpikeRealign ${CPU_SOURCES})
    target_compile_options(SpikeRealign PRIVATE ${_OPT_FLAGS})
    target_compile_definitions(SpikeRealign PRIVATE NDEBUG USE_HIP)
    target_include_directories(SpikeRealign PRIVATE
        $ENV{ROCM_PATH}/include /opt/rocm/include
    )
    target_link_libraries(SpikeRealign PRIVATE
        sr_hip_obj
        $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
        -L$ENV{ROCM_PATH}/lib -L/opt/rocm/lib -lamdhip64
    )
    target_link_options(SpikeRealign PRIVATE
        -fgpu-rdc --offload-arch=${_HIP_ARCH_COMMA}
    )
    set_target_properties(SpikeRealign PROPERTIES LINKER "${_HIPCC}")
    install(TARGETS SpikeRealign DESTINATION bin)
endif()

# =============================================================================
# SYCL build
# =============================================================================
if(_SYCL_AVAILABLE)
    find_program(_OCLOC ocloc
        HINTS
            $ENV{ONEAPI_ROOT}/compiler/latest/bin
            /opt/intel/oneapi/compiler/latest/bin
            /usr/bin
        NO_DEFAULT_PATH
    )
    if(NOT _OCLOC)
        find_program(_OCLOC ocloc)
    endif()

    if(_OCLOC)
        if(NOT DEFINED SR_SYCL_DEVICES)
            set(SR_SYCL_DEVICES "mtl-m,acm-g10,acm-g11,dg2,pvc")
        endif()
        set(_SYCL_TARGET_FLAG "-fsycl-targets=spir64_gen")
        set(_SYCL_DEVICE_FLAG "-Xs \"-device ${SR_SYCL_DEVICES}\"")
        message(STATUS "  ocloc: ${_OCLOC}  AOT devices: ${SR_SYCL_DEVICES}")
    else()
        set(_SYCL_TARGET_FLAG "-fsycl-targets=spir64")
        set(_SYCL_DEVICE_FLAG "")
        message(STATUS "  ocloc not found — JIT-only SYCL build")
    endif()

    add_executable(SpikeRealign_sycl
        SpikeRealign.cpp
        realign_xcorr_omp.cpp
        realign_xcorr_dispatch.cpp
        realign_xcorr_sycl.cpp
    )

    if(_OCLOC)
        target_compile_options(SpikeRealign_sycl PRIVATE
            -O3 -march=native -fsycl ${_SYCL_TARGET_FLAG}
            -Wno-unused-command-line-argument -Wall -Wextra
        )
        target_link_options(SpikeRealign_sycl PRIVATE
            -fsycl ${_SYCL_TARGET_FLAG} "SHELL:${_SYCL_DEVICE_FLAG}"
        )
    else()
        target_compile_options(SpikeRealign_sycl PRIVATE
            -O3 -march=native -fsycl ${_SYCL_TARGET_FLAG} -Wall -Wextra
        )
        target_link_options(SpikeRealign_sycl PRIVATE
            -fsycl ${_SYCL_TARGET_FLAG}
        )
    endif()

    target_compile_definitions(SpikeRealign_sycl PRIVATE NDEBUG USE_SYCL)
    target_link_libraries(SpikeRealign_sycl PRIVATE
        $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
    )
    install(TARGETS SpikeRealign_sycl DESTINATION bin)
    install(PROGRAMS $<TARGET_FILE:SpikeRealign_sycl>
            DESTINATION bin RENAME SpikeRealign)
endif()

# =============================================================================
# CPU reference binary — always built
# =============================================================================
add_executable(SpikeRealign_cpu ${CPU_SOURCES})
target_compile_options(SpikeRealign_cpu PRIVATE ${_OPT_FLAGS})
target_compile_definitions(SpikeRealign_cpu PRIVATE NDEBUG)
if(OpenMP_CXX_FOUND)
    target_link_libraries(SpikeRealign_cpu PRIVATE OpenMP::OpenMP_CXX)
endif()
install(TARGETS SpikeRealign_cpu DESTINATION bin)
