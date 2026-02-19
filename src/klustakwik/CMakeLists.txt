cmake_minimum_required(VERSION 3.20)

# Declare without CUDA first — we probe for it below so the build does not
# fail on machines without a CUDA toolkit installed.
project(KlustaKwik LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- CUDA probe -------------------------------------------------------------
# check_language(CUDA) sets CMAKE_CUDA_COMPILER if nvcc is found.
# We then enable CUDA only when both the compiler and USE_CUDA are available.
include(CheckLanguage)
check_language(CUDA)

option(USE_CUDA "Build with CUDA GPU acceleration" ON)

if(USE_CUDA AND CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    # sm_86: Ampere  (RTX 30xx)
    # sm_89: Ada Lovelace (RTX 40xx)
    # sm_100: Blackwell datacenter (B100/B200)
    # sm_120: Blackwell consumer (RTX 5070 Ti, 5080, 5090)
    set(CMAKE_CUDA_ARCHITECTURES "86;89;100;120")
    set(_CUDA_AVAILABLE TRUE)
    message(STATUS "KlustaKwik: CUDA build enabled (${CMAKE_CUDA_COMPILER})")
else()
    set(_CUDA_AVAILABLE FALSE)
    if(USE_CUDA AND NOT CMAKE_CUDA_COMPILER)
        message(STATUS "KlustaKwik: nvcc not found — falling back to CPU-only build")
    else()
        message(STATUS "KlustaKwik: CPU-only build")
    endif()
endif()

# ---- Sources ----------------------------------------------------------------
set(CPU_SOURCES
    KlustaKwik.cpp
    KK.cpp
    param.c
)

set(HEADERS
    KlustaKwik.h
    KK.h
    KK_cuda.h
    Array.h
    KlustaSave.h
    param.h
)

find_package(OpenMP)

# ---- CPU-only build ---------------------------------------------------------
if(NOT _CUDA_AVAILABLE)
    add_executable(KlustaKwik ${CPU_SOURCES} ${HEADERS})
    target_compile_options(KlustaKwik PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -Wall -Wextra>
    )
    target_compile_definitions(KlustaKwik PRIVATE NDEBUG)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(KlustaKwik PRIVATE OpenMP::OpenMP_CXX)
    endif()
    install(TARGETS KlustaKwik DESTINATION bin)
    return()
endif()

# ---- CUDA build -------------------------------------------------------------
find_package(CUDAToolkit REQUIRED)

add_executable(KlustaKwik ${CPU_SOURCES} KK_cuda.cu ${HEADERS})

target_compile_options(KlustaKwik PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-O3 -march=native -Wall -Wextra>
    $<$<COMPILE_LANGUAGE:CUDA>:
        -O3
        --use_fast_math
        -Xptxas -v
        --extended-lambda
    >
)

target_compile_definitions(KlustaKwik PRIVATE
    NDEBUG
    USE_CUDA
)

target_link_libraries(KlustaKwik PRIVATE
    CUDA::cudart
    m
    $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>
)

# CPU fallback always built alongside the CUDA binary
add_executable(KlustaKwik_cpu ${CPU_SOURCES} ${HEADERS})
target_compile_options(KlustaKwik_cpu PRIVATE -O3 -march=native)
target_compile_definitions(KlustaKwik_cpu PRIVATE NDEBUG)
if(OpenMP_CXX_FOUND)
    target_link_libraries(KlustaKwik_cpu PRIVATE OpenMP::OpenMP_CXX)
endif()

install(TARGETS KlustaKwik KlustaKwik_cpu DESTINATION bin)

message(STATUS "CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "CUDA toolkit: ${CUDAToolkit_VERSION}")
