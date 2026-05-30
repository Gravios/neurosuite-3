cmake_minimum_required(VERSION 3.21)

# =============================================================================
# SYCL compiler auto-selection  (must happen BEFORE project())
#
# CMake locks the compiler once project() is called and cannot change it
# afterwards.  We therefore probe for icpx here — before project() — and
# set CMAKE_CXX_COMPILER only when:
#   • USE_SYCL is not explicitly OFF, AND
#   • the caller has not already specified a compiler (via -DCMAKE_CXX_COMPILER
#     or the CXX environment variable), AND
#   • icpx is found in the standard oneAPI install locations AND identifies
#     itself as Intel's compiler.
#
# Behaviour summary:
#   cmake ..                           → auto-selects icpx when found
#   cmake .. -DUSE_SYCL=OFF            → always uses the default system compiler
#   cmake .. -DCMAKE_CXX_COMPILER=g++  → respects the explicit choice
# =============================================================================
if(NOT DEFINED USE_SYCL OR NOT USE_SYCL STREQUAL "OFF")
    if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
        find_program(_ICPX_EARLY icpx
            HINTS
                $ENV{ONEAPI_ROOT}/compiler/latest/bin
                /opt/intel/oneapi/compiler/latest/bin
            NO_DEFAULT_PATH)
        if(_ICPX_EARLY)
            execute_process(
                COMMAND "${_ICPX_EARLY}" --version
                OUTPUT_VARIABLE _ICPX_VER
                ERROR_VARIABLE  _ICPX_VER
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(_ICPX_VER MATCHES "Intel")
                get_filename_component(_ICPX_DIR "${_ICPX_EARLY}" DIRECTORY)
                set(CMAKE_CXX_COMPILER "${_ICPX_EARLY}" CACHE FILEPATH
                    "C++ compiler — auto-selected icpx for SYCL" FORCE)
                set(CMAKE_C_COMPILER "${_ICPX_DIR}/icx" CACHE FILEPATH
                    "C compiler — auto-selected icx for SYCL" FORCE)
                message(STATUS
                    "KiloKlustaKwik: auto-selected icpx (override: -DCMAKE_CXX_COMPILER=)")
            endif()
        endif()
    endif()
endif()

project(KiloKlustaKwik VERSION 2.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          OFF)
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Default build type
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "KiloKlustaKwik: CMAKE_BUILD_TYPE defaulted to Release")
endif()

# ---------------------------------------------------------------------------
# Install layout
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# =============================================================================
# GPU backend options
#
#   USE_CUDA — NVIDIA GPUs via nvcc         (requires CUDA toolkit)
#   USE_HIP  — AMD GPUs via hipcc/ROCm      (requires ROCm >= 5.0)
#   USE_SYCL — Intel Arc/Xe via icpx/oneAPI (requires oneAPI Base Toolkit >= 2023.1)
#
# All three default ON; whichever toolkit is present is auto-selected.
# Priority when multiple are found: CUDA > HIP > SYCL.
# Force a specific backend:
#   CUDA only:  cmake -DUSE_HIP=OFF  -DUSE_SYCL=OFF
#   HIP  only:  cmake -DUSE_CUDA=OFF -DUSE_SYCL=OFF
#   SYCL only:  cmake -DUSE_CUDA=OFF -DUSE_HIP=OFF
# =============================================================================
option(USE_CUDA "Build with NVIDIA CUDA GPU acceleration"       ON)
option(USE_HIP  "Build with AMD ROCm/HIP GPU acceleration"      ON)
option(USE_SYCL "Build with Intel SYCL/oneAPI GPU acceleration" ON)

include(CheckLanguage)

# =============================================================================
# CUDA probe
# =============================================================================
set(_CUDA_ACTIVE FALSE)
if(USE_CUDA)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD          17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        set(CMAKE_CUDA_ARCHITECTURES     "86;89;100;120"
            CACHE STRING "CUDA architectures to target")
        set(_CUDA_ACTIVE TRUE)
        message(STATUS "KiloKlustaKwik: CUDA available — ${CMAKE_CUDA_COMPILER}")
    else()
        message(STATUS "KiloKlustaKwik: CUDA requested but nvcc not found")
    endif()
endif()

# =============================================================================
# HIP probe (skipped when CUDA already selected)
# =============================================================================
set(_HIP_ACTIVE FALSE)
if(USE_HIP AND NOT _CUDA_ACTIVE)
    find_program(_HIPCC hipcc
        HINTS $ENV{ROCM_PATH}/bin /opt/rocm/bin /usr/bin)
    if(_HIPCC)
        # Prefer the CMake-native hip package when available
        find_package(hip QUIET
            PATHS $ENV{ROCM_PATH}/lib/cmake/hip /opt/rocm/lib/cmake/hip)
        set(_HIP_ACTIVE TRUE)
        message(STATUS "KiloKlustaKwik: HIP available — ${_HIPCC}")
        if(hip_FOUND)
            message(STATUS "  ROCm CMake package version: ${hip_VERSION}")
        endif()
    else()
        message(STATUS
            "KiloKlustaKwik: HIP requested but hipcc not found — "
            "install ROCm: https://rocm.docs.amd.com/")
    endif()
endif()

# =============================================================================
# SYCL probe (skipped when CUDA or HIP already selected)
# =============================================================================
set(_SYCL_ACTIVE FALSE)
if(USE_SYCL AND NOT _CUDA_ACTIVE AND NOT _HIP_ACTIVE)
    find_program(_ICPX icpx
        HINTS
            $ENV{ONEAPI_ROOT}/compiler/latest/bin
            /opt/intel/oneapi/compiler/latest/bin
        NO_DEFAULT_PATH)
    if(_ICPX)
        execute_process(
            COMMAND "${_ICPX}" --version
            OUTPUT_VARIABLE _ICPX_VER
            ERROR_VARIABLE  _ICPX_VER
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_ICPX_VER MATCHES "Intel")
            set(_SYCL_ACTIVE TRUE)
            message(STATUS "KiloKlustaKwik: SYCL available — ${_ICPX}")
            message(STATUS "  ${_ICPX_VER}")
        else()
            message(STATUS
                "KiloKlustaKwik: icpx found but does not identify as Intel — "
                "skipping SYCL build (version: ${_ICPX_VER})")
            unset(_ICPX CACHE)
        endif()
    else()
        message(STATUS
            "KiloKlustaKwik: SYCL requested but icpx not found — "
            "install oneAPI: https://www.intel.com/content/www/us/en/developer/"
            "tools/oneapi/base-toolkit.html")
    endif()
endif()

# =============================================================================
# Backend summary
# =============================================================================
if(_CUDA_ACTIVE)
    message(STATUS "KiloKlustaKwik: active backend → CUDA  (NVIDIA)")
elseif(_HIP_ACTIVE)
    message(STATUS "KiloKlustaKwik: active backend → HIP   (AMD ROCm)")
elseif(_SYCL_ACTIVE)
    message(STATUS "KiloKlustaKwik: active backend → SYCL  (Intel Arc/Xe)")
else()
    message(STATUS "KiloKlustaKwik: active backend → CPU only (no GPU toolkit found)")
endif()

# =============================================================================
# Shared xcorr kernel (lives in src/libklustersshared/src/xcorr — no Qt/yaml
# dependency).  Relocated from src/shared/xcorr in patch66.
# The OMP + dispatch sources go into every executable via _cpu_srcs.
# GPU backend sources are added per-target below, matching how KK_cuda.cu
# and KK_hip.cpp are handled.
# =============================================================================
set(_xcorr_dir "${CMAKE_CURRENT_SOURCE_DIR}/../libklustersshared/src/xcorr")

# =============================================================================
# Common sources and dependencies
# =============================================================================
set(_cpu_srcs KlustaKwik.cpp KK.cpp KlustaKwikYaml.cpp KK_prior.cpp param.c
    dipsplit.cpp
    proxy_isi.cpp
    wave_knn_split.cpp
    cluster_hull_split.cpp
    klusters_realign.cpp
    per_channel_split.cpp
    xcorr_match.cpp
    clust_quality.cpp
    adapt_model.cpp
    shiftprobe_disabled.cpp
    ${_xcorr_dir}/realign_xcorr_omp.cpp
    ${_xcorr_dir}/realign_xcorr_dispatch.cpp
    ${_xcorr_dir}/realign_center.cpp)
set(_headers  KlustaKwik.h KK.h KK_cuda.h KK_sycl.h KK_hip.h
              Array.h KlustaSave.h KlustaKwikYaml.h param.h
              dipsplit.h
              proxy_isi.h
              wave_knn_split.h
              cluster_hull_split.h
              klusters_realign.h
              per_channel_split.h
              xcorr_match.h
              clust_quality.h
              adapt_model.h)

find_package(OpenMP)
find_package(yaml-cpp REQUIRED)

# Generator-expression compile flags applied per-target via INTERFACE library
# so they don't accidentally bleed onto CUDA or SYCL compilation units.
add_library(_kk_exp_cxx_flags INTERFACE)
target_compile_options(_kk_exp_cxx_flags INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -Wall -Wextra>)
target_compile_definitions(_kk_exp_cxx_flags INTERFACE
    $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)
# Shared xcorr header propagates to CPU, HIP, SYCL targets that link _kk_exp_cxx_flags.
# The CUDA target is wired manually and gets an explicit include_directories below.
target_include_directories(_kk_exp_cxx_flags INTERFACE "${_xcorr_dir}")

# =============================================================================
# Helper: configure one KiloKlustaKwik executable (avoids repetition)
# Usage: kk_exp_configure_exe(<target> [LINK_LIBS lib…])
# =============================================================================
function(kk_exp_configure_exe _tgt)
    cmake_parse_arguments(_A "" "" "LINK_LIBS" ${ARGN})
    target_link_libraries(${_tgt} PRIVATE _kk_exp_cxx_flags)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${_tgt} PRIVATE OpenMP::OpenMP_CXX)
    endif()
    target_link_libraries(${_tgt} PRIVATE yaml-cpp)
    foreach(_lib IN LISTS _A_LINK_LIBS)
        target_link_libraries(${_tgt} PRIVATE ${_lib})
    endforeach()
endfunction()

# =============================================================================
# CPU reference binary — always built
# =============================================================================
add_executable(KiloKlustaKwik_cpu ${_cpu_srcs} ${_headers})
kk_exp_configure_exe(KiloKlustaKwik_cpu)
install(TARGETS KiloKlustaKwik_cpu DESTINATION "${CMAKE_INSTALL_BINDIR}")

# =============================================================================
# CUDA build  (NVIDIA)
# =============================================================================
if(_CUDA_ACTIVE)
    find_package(CUDAToolkit REQUIRED)

    # shiftprobe_cuda.cu is no longer compiled — see shiftprobe_disabled.cpp
    # for the rationale.
    add_executable(KiloKlustaKwik ${_cpu_srcs} KK_cuda.cu ${_xcorr_dir}/realign_xcorr_cuda.cu ${_headers})
    target_include_directories(KiloKlustaKwik PRIVATE "${_xcorr_dir}")
    target_compile_options(KiloKlustaKwik PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -Wall -Wextra>>
        $<$<COMPILE_LANGUAGE:CUDA>:
            -O3 --use_fast_math -Xptxas -v --extended-lambda>)
    target_compile_definitions(KiloKlustaKwik PRIVATE
        USE_CUDA $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)
    target_link_libraries(KiloKlustaKwik PRIVATE CUDA::cudart m yaml-cpp)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(KiloKlustaKwik PRIVATE OpenMP::OpenMP_CXX)
    endif()
    install(TARGETS KiloKlustaKwik DESTINATION "${CMAKE_INSTALL_BINDIR}")

    message(STATUS "  CUDA architectures : ${CMAKE_CUDA_ARCHITECTURES}")
    message(STATUS "  CUDA toolkit       : ${CUDAToolkit_VERSION}")
endif()

# =============================================================================
# HIP build  (AMD ROCm)
# =============================================================================
if(_HIP_ACTIVE)
    # GPU architecture targets — override with -DKK_HIP_ARCHS="gfx1100;gfx90a"
    # RDNA2: gfx1030/1031/1032  RDNA3: gfx1100/1101/1102
    # CDNA1: gfx908              CDNA2: gfx90a
    set(KK_EXP_HIP_ARCHS "gfx1030;gfx1031;gfx1100;gfx1101;gfx90a;gfx908"
        CACHE STRING "HIP GPU architectures to target")
    string(REPLACE ";" "," _hip_archs_comma "${KK_EXP_HIP_ARCHS}")

    # KK_hip.cpp contains __global__ kernels and must be compiled by hipcc.
    # Build it as an OBJECT library under hipcc, then link the objects into
    # the main executable compiled with the system CXX — avoids changing
    # CMAKE_CXX_COMPILER globally.
    add_library(kk_exp_hip_kernels OBJECT KK_hip.cpp)
    set_property(TARGET kk_exp_hip_kernels PROPERTY CXX_COMPILER "${_HIPCC}")
    set_target_properties(kk_exp_hip_kernels PROPERTIES CXX_COMPILER_LAUNCHER "")
    target_compile_options(kk_exp_hip_kernels PRIVATE
        -O3 -march=native
        --offload-arch=${_hip_archs_comma}
        -fgpu-rdc
        -Wall -Wextra)
    target_compile_definitions(kk_exp_hip_kernels PRIVATE
        USE_HIP $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)
    target_include_directories(kk_exp_hip_kernels PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_xcorr_dir}"
        $<$<BOOL:$ENV{ROCM_PATH}>:$ENV{ROCM_PATH}/include>
        /opt/rocm/include)

    # xcorr HIP kernels — compiled the same way as kk_exp_hip_kernels
    add_library(xcorr_hip_kernels_exp OBJECT ${_xcorr_dir}/realign_xcorr_hip.hip)
    set_property(TARGET xcorr_hip_kernels_exp PROPERTY CXX_COMPILER "${_HIPCC}")
    set_target_properties(xcorr_hip_kernels_exp PROPERTIES CXX_COMPILER_LAUNCHER "")
    target_compile_options(xcorr_hip_kernels_exp PRIVATE
        -O3 -march=native
        --offload-arch=${_hip_archs_comma}
        -fgpu-rdc
        -Wall -Wextra)
    target_compile_definitions(xcorr_hip_kernels_exp PRIVATE
        USE_HIP $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)
    target_include_directories(xcorr_hip_kernels_exp PRIVATE
        "${_xcorr_dir}"
        $<$<BOOL:$ENV{ROCM_PATH}>:$ENV{ROCM_PATH}/include>
        /opt/rocm/include)

    # shiftprobe_hip.cpp is no longer compiled — see shiftprobe_disabled.cpp
    # for the rationale.

    add_executable(KiloKlustaKwik ${_cpu_srcs} ${_headers})
    target_compile_definitions(KiloKlustaKwik PRIVATE
        USE_HIP $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)
    target_include_directories(KiloKlustaKwik PRIVATE
        $<$<BOOL:$ENV{ROCM_PATH}>:$ENV{ROCM_PATH}/include>
        /opt/rocm/include)
    target_link_libraries(KiloKlustaKwik PRIVATE
        _kk_exp_cxx_flags
        kk_exp_hip_kernels
        xcorr_hip_kernels_exp
        yaml-cpp)
    if(hip_FOUND)
        target_link_libraries(KiloKlustaKwik PRIVATE hip::host)
    else()
        # Fall back to raw flags when the CMake package is unavailable
        target_link_options(KiloKlustaKwik PRIVATE
            $<$<BOOL:$ENV{ROCM_PATH}>:-L$ENV{ROCM_PATH}/lib>
            -L/opt/rocm/lib -lamdhip64)
    endif()
    if(OpenMP_CXX_FOUND)
        target_link_libraries(KiloKlustaKwik PRIVATE OpenMP::OpenMP_CXX)
    endif()
    # hipcc must drive the final link step when -fgpu-rdc is used
    target_link_options(KiloKlustaKwik PRIVATE
        -fgpu-rdc --offload-arch=${_hip_archs_comma})
    set_target_properties(KiloKlustaKwik PROPERTIES LINKER "${_HIPCC}")

    install(TARGETS KiloKlustaKwik DESTINATION "${CMAKE_INSTALL_BINDIR}")
    message(STATUS "  HIP architectures  : ${KK_EXP_HIP_ARCHS}")
endif()

# =============================================================================
# SYCL build  (Intel Arc/Xe)
# =============================================================================
if(_SYCL_ACTIVE)
    # ocloc enables ahead-of-time (AOT) GPU ISA compilation.
    # Without it we fall back to JIT (spir64): portable but ~1-2 s first-run cost.
    find_program(_OCLOC ocloc
        HINTS
            $ENV{ONEAPI_ROOT}/compiler/latest/bin
            /opt/intel/oneapi/compiler/latest/bin
            /usr/bin)

    if(_OCLOC)
        # AOT device codes — override with -DKK_SYCL_DEVICES="mtl-m,acm-g10"
        # mtl-m    Meteor Lake iGPU   acm-g10  Arc A770/A750
        # acm-g11  Arc A380/A310      pvc      Ponte Vecchio
        set(KK_EXP_SYCL_DEVICES "mtl-m,acm-g10,acm-g11,dg2,pvc"
            CACHE STRING "ocloc device targets for AOT SYCL compilation")
        set(_sycl_target_flag "-fsycl-targets=spir64_gen")
        set(_sycl_device_flag "-Xs \"-device ${KK_EXP_SYCL_DEVICES}\"")
        message(STATUS "  ocloc              : ${_OCLOC}")
        message(STATUS "  AOT devices        : ${KK_EXP_SYCL_DEVICES}")
    else()
        set(_sycl_target_flag "-fsycl-targets=spir64")
        set(_sycl_device_flag "")
        message(STATUS "  ocloc not found — JIT-only SYCL build (spir64)")
        message(STATUS "  For AOT: sudo apt install intel-ocloc")
    endif()

    # param.c is plain C and must not receive -fsycl.  Build it separately.
    add_library(param_obj_exp OBJECT param.c)
    target_compile_options(param_obj_exp PRIVATE -O3)
    target_compile_definitions(param_obj_exp PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)

    # KlustaKwikYaml.cpp is plain C++ (yaml-cpp, no SYCL kernels).
    # Must NOT be compiled with -fsycl — build as a separate OBJECT library.
    add_library(yaml_obj_exp OBJECT KlustaKwikYaml.cpp)
    target_compile_options(yaml_obj_exp PRIVATE -O3 -Wall -Wextra)
    target_compile_definitions(yaml_obj_exp PRIVATE
        $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)

    # KlustaKwikYaml.cpp is intentionally NOT listed here — it is supplied
    # via $<TARGET_OBJECTS:yaml_obj_exp> below.  Listing it in both places
    # causes icpx to emit two object files (one with -fsycl, one without)
    # that both define kkReadYamlSpikeParams, producing a multiple-definition
    # link error on Intel Arc / oneAPI builds.
    # shiftprobe_sycl.cpp is no longer compiled — see shiftprobe_disabled.cpp
    # for the rationale.
    add_executable(KiloKlustaKwik_sycl
        KlustaKwik.cpp KK.cpp KK_sycl.cpp KK_prior.cpp
        dipsplit.cpp
        proxy_isi.cpp
        wave_knn_split.cpp
        cluster_hull_split.cpp
        klusters_realign.cpp
        per_channel_split.cpp
        xcorr_match.cpp
        clust_quality.cpp
        adapt_model.cpp
        shiftprobe_disabled.cpp
        ${_xcorr_dir}/realign_xcorr_omp.cpp
        ${_xcorr_dir}/realign_xcorr_dispatch.cpp
        ${_xcorr_dir}/realign_xcorr_sycl.cpp
        ${_xcorr_dir}/realign_center.cpp
        $<TARGET_OBJECTS:param_obj_exp>
        $<TARGET_OBJECTS:yaml_obj_exp>
        ${_headers})

    # -Xs (ocloc device flag) is link-time only; suppress the noisy warning
    # it generates when passed at compile time too.
    if(_OCLOC)
        target_compile_options(KiloKlustaKwik_sycl PRIVATE
            -O3 -march=native -fsycl ${_sycl_target_flag}
            -Wno-unused-command-line-argument -Wall -Wextra)
        target_link_options(KiloKlustaKwik_sycl PRIVATE
            -fsycl ${_sycl_target_flag}
            "SHELL:${_sycl_device_flag}")
    else()
        target_compile_options(KiloKlustaKwik_sycl PRIVATE
            -O3 -march=native -fsycl ${_sycl_target_flag}
            -Wall -Wextra)
        target_link_options(KiloKlustaKwik_sycl PRIVATE
            -fsycl ${_sycl_target_flag})
    endif()

    target_include_directories(KiloKlustaKwik_sycl PRIVATE "${_xcorr_dir}")
    target_compile_definitions(KiloKlustaKwik_sycl PRIVATE
        USE_SYCL $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)
    target_link_libraries(KiloKlustaKwik_sycl PRIVATE yaml-cpp)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(KiloKlustaKwik_sycl PRIVATE OpenMP::OpenMP_CXX)
    endif()

    set_target_properties(KiloKlustaKwik_sycl PROPERTIES
        OUTPUT_NAME KiloKlustaKwik_sycl)

    install(TARGETS KiloKlustaKwik_sycl DESTINATION "${CMAKE_INSTALL_BINDIR}")
    # Also install under the canonical name so callers need not know the backend
    install(PROGRAMS "$<TARGET_FILE:KiloKlustaKwik_sycl>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME KiloKlustaKwik)

    message(STATUS "  SYCL compiler      : ${_ICPX}")
endif()

# CPU-only: also install KiloKlustaKwik_cpu as the canonical KiloKlustaKwik
if(NOT _CUDA_ACTIVE AND NOT _HIP_ACTIVE AND NOT _SYCL_ACTIVE)
    install(PROGRAMS "$<TARGET_FILE:KiloKlustaKwik_cpu>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
        RENAME KiloKlustaKwik)
endif()

# Ergonomic wrapper for the -RefineExisting workflow (KK::RefineExistingClustering).
# Auto-detects <session>.clu.<g> as the seed and <session>.chunks.<g> as the
# drift-chunk file; forwards remaining flags to the binary.
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/KlustaKwikRefine"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
