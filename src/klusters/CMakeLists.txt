cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# SYCL needs to be declared as a project language before project() if using
# Intel oneAPI; we probe for it conditionally below.
# ---------------------------------------------------------------------------
project(klusters VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# OpenMP — always attempted, silently skipped if unavailable.
# ---------------------------------------------------------------------------
find_package(OpenMP)

# ---------------------------------------------------------------------------
# GPU backend detection — all three are probed automatically.
# Any can be individually disabled: -DUSE_CUDA=OFF / -DUSE_HIP=OFF / -DUSE_SYCL=OFF
# ---------------------------------------------------------------------------

# --- CUDA (NVIDIA) ----------------------------------------------------------
option(USE_CUDA "Enable CUDA GPU backend (NVIDIA)" ON)
if(USE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
        message(STATUS "CUDA backend enabled — compiler: ${CMAKE_CUDA_COMPILER}")
    else()
        set(USE_CUDA OFF)
        message(STATUS "CUDA backend: compiler not found, disabled")
    endif()
endif()

# --- HIP (AMD ROCm) ---------------------------------------------------------
option(USE_HIP "Enable HIP GPU backend (AMD ROCm)" ON)
if(USE_HIP)
    # ROCm ships its own CMake package. ROCM_PATH is the conventional env var.
    if(NOT DEFINED ROCM_PATH AND DEFINED ENV{ROCM_PATH})
        set(ROCM_PATH "$ENV{ROCM_PATH}")
    endif()
    if(NOT DEFINED ROCM_PATH)
        set(ROCM_PATH "/opt/rocm")
    endif()

    list(APPEND CMAKE_PREFIX_PATH "${ROCM_PATH}")
    find_package(hip QUIET)

    if(hip_FOUND)
        # Enable HIP as a CMake language so .hip files are compiled by hipcc.
        include(CheckLanguage)
        check_language(HIP)
        if(CMAKE_HIP_COMPILER)
            enable_language(HIP)
            set(CMAKE_HIP_STANDARD 17)
            message(STATUS "HIP backend enabled — compiler: ${CMAKE_HIP_COMPILER}")
        else()
            set(USE_HIP OFF)
            message(STATUS "HIP backend: hipcc not found, disabled")
        endif()
    else()
        set(USE_HIP OFF)
        message(STATUS "HIP backend: ROCm not found at ${ROCM_PATH}, disabled")
    endif()
endif()

# --- SYCL (Intel oneAPI / Arc) ----------------------------------------------
option(USE_SYCL "Enable SYCL GPU backend (Intel oneAPI)" ON)
if(USE_SYCL)
    # Intel oneAPI uses icpx. Detect by looking for it on PATH or via the
    # IntelDPCPP CMake package that ships with oneAPI Base Toolkit.
    find_package(IntelDPCPP QUIET)
    if(IntelDPCPP_FOUND)
        message(STATUS "SYCL backend enabled — Intel oneAPI DPC++ found")
    else()
        # Fallback: check if CXX compiler is already icpx (user set CC/CXX).
        if(CMAKE_CXX_COMPILER MATCHES "icpx")
            message(STATUS "SYCL backend enabled — icpx detected as CXX compiler")
        else()
            set(USE_SYCL OFF)
            message(STATUS "SYCL backend: Intel oneAPI not found, disabled")
        endif()
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml PrintSupport)
qt_standard_project_setup()

find_package(LibKlustersShared 2.0.0 REQUIRED)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config-klusters.h.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/config-klusters.h
)

set(CMAKE_INCLUDE_CURRENT_DIR TRUE)

add_subdirectory(src)
add_subdirectory(po)
add_subdirectory(doc)

include(CPack)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cluster cutting application")
set(CPACK_PACKAGE_NAME "klusters")
set(CPACK_PACKAGE_VERSION_MAJOR "2")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
