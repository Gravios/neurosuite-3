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
        find_package(CUDAToolkit REQUIRED)
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
    # ── Step 1: find icpx on PATH or in canonical oneAPI install locations ──
    find_program(ICPX_EXECUTABLE
        NAMES icpx
        HINTS
            "$ENV{CMPLR_ROOT}/bin"
            "$ENV{ONEAPI_ROOT}/compiler/latest/bin"
            "$ENV{ONEAPI_ROOT}/compiler/latest/linux/bin"
            "/opt/intel/oneapi/compiler/latest/bin"
            "/opt/intel/oneapi/compiler/latest/linux/bin"
        DOC "Intel oneAPI DPC++ compiler (icpx)"
    )

    if(ICPX_EXECUTABLE)
        message(STATUS "SYCL backend: icpx found at ${ICPX_EXECUTABLE}")
        # ── Step 2: prefer the IntelSYCL CMake package; fall back to IntelDPCPP ─
        # Intel renamed the package in oneAPI 2024+.  IntelDPCPP still works but
        # emits a deprecation warning and may not export targets in newer toolkits.
        find_package(IntelSYCL QUIET)
        if(IntelSYCL_FOUND)
            message(STATUS "SYCL backend enabled — IntelSYCL CMake package loaded")
        else()
            find_package(IntelDPCPP QUIET)
            if(IntelDPCPP_FOUND)
                message(STATUS "SYCL backend enabled — IntelDPCPP CMake package loaded (consider upgrading to IntelSYCL)")
            else()
                # Package not found but we have the compiler — set flags manually.
                message(STATUS "SYCL backend enabled — icpx found, CMake package absent (flags set manually)")
            endif()
        endif()
        # Force the CXX compiler to icpx so CMake uses -fsycl.
        # Only effective on a clean configure; if the user already configured
        # with a different CXX the cached value takes precedence.
        if(NOT CMAKE_CXX_COMPILER MATCHES "icpx")
            message(STATUS "SYCL: setting CMAKE_CXX_COMPILER to ${ICPX_EXECUTABLE}")
            set(CMAKE_CXX_COMPILER "${ICPX_EXECUTABLE}" CACHE FILEPATH
                "C++ compiler (overridden to icpx for SYCL)" FORCE)
        endif()
    elseif(CMAKE_CXX_COMPILER MATCHES "icpx")
        # User explicitly set CXX=icpx before running cmake.
        message(STATUS "SYCL backend enabled — icpx detected as active CXX compiler")
        find_package(IntelSYCL QUIET)
        if(NOT IntelSYCL_FOUND)
            find_package(IntelDPCPP QUIET)
        endif()
    else()
        # ── icpx not found anywhere ──────────────────────────────────────────
        set(USE_SYCL OFF)
        message(STATUS "SYCL backend: disabled")
        message(STATUS "  Reason: icpx (Intel oneAPI DPC++ compiler) not found.")
        message(STATUS "  To enable SYCL support, install the Intel oneAPI Base Toolkit:")
        message(STATUS "    https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html")
        message(STATUS "  Then source the environment before configuring:")
        message(STATUS "    source /opt/intel/oneapi/setvars.sh")
        message(STATUS "    cmake -DUSE_SYCL=ON ..")
        message(STATUS "  Or disable this message with: -DUSE_SYCL=OFF")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml PrintSupport)
qt_standard_project_setup()

find_package(LibKlustersShared 2.1.0 REQUIRED
    HINTS
        "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibKlustersShared"
        /usr/local/lib/cmake/LibKlustersShared
        /usr/lib/cmake/LibKlustersShared)

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
