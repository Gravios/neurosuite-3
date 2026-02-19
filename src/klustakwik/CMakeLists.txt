cmake_minimum_required(VERSION 3.20)
project(KlustaKwik LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# ---- GPU architecture targets -----------------------------------------------
# sm_86: Ampere  (RTX 30xx)
# sm_89: Ada Lovelace (RTX 40xx)
# sm_120: Blackwell consumer (RTX 5070 Ti, 5080, 5090)
# sm_100: Blackwell datacenter (B100/B200)
set(CMAKE_CUDA_ARCHITECTURES "86;89;100;120")

# ---- Options ----------------------------------------------------------------
option(USE_CUDA "Build with CUDA GPU acceleration" ON)

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

# ---- CPU-only build ---------------------------------------------------------
if(NOT USE_CUDA)
    find_package(OpenMP)
    add_executable(KlustaKwik ${CPU_SOURCES} ${HEADERS})
    target_compile_options(KlustaKwik PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -Wall -Wextra>
    )
    target_compile_definitions(KlustaKwik PRIVATE NDEBUG)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(KlustaKwik PRIVATE OpenMP::OpenMP_CXX)
    endif()
    return()
endif()

# ---- CUDA build -------------------------------------------------------------
find_package(CUDAToolkit REQUIRED)
find_package(OpenMP)

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

# ---- CPU-fallback build (always available as KlustaKwik_cpu) ----------------
add_executable(KlustaKwik_cpu ${CPU_SOURCES} ${HEADERS})
target_compile_options(KlustaKwik_cpu PRIVATE -O3 -march=native)
target_compile_definitions(KlustaKwik_cpu PRIVATE NDEBUG)
if(OpenMP_CXX_FOUND)
    target_link_libraries(KlustaKwik_cpu PRIVATE OpenMP::OpenMP_CXX)
endif()

# ---- Install ----------------------------------------------------------------
install(TARGETS KlustaKwik DESTINATION bin)
if(USE_CUDA)
    install(TARGETS KlustaKwik_cpu DESTINATION bin)
endif()

# ---- Info -------------------------------------------------------------------
message(STATUS "CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")
message(STATUS "CUDA toolkit: ${CUDAToolkit_VERSION}")
