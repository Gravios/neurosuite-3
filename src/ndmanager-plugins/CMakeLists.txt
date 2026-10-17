cmake_minimum_required(VERSION 3.22...3.31)
project(ndmanager-plugins VERSION 1.4.14 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Language standards
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
    message(STATUS "ndmanager-plugins: CMAKE_BUILD_TYPE defaulted to Release")
endif()

# ---------------------------------------------------------------------------
# Install layout
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Shared CMake modules — add_manpage() defined here, visible to all subdirs
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NdmManPage)

# ---------------------------------------------------------------------------
# CPack
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME                "ndmanager-plugins")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Data processing plugins for NDManager")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR       "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR       "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH       "${PROJECT_VERSION_PATCH}")
include(CPack)

# ---------------------------------------------------------------------------
# The rest of the monorepo
# ---------------------------------------------------------------------------
# The plugins compile sources from libklustersshared (progressbar.cpp, headers)
# and use libneurosuite-core, both beside this directory in neurosuite-3.  Name
# them from here, not from CMAKE_SOURCE_DIR, so the plugins also configure on
# their own (cmake -S src/ndmanager-plugins).  Neurosuite::core is added when
# the parent build has not already added it.
if(NOT DEFINED NS_BUILD_TESTS)
    option(NS_BUILD_TESTS "Build the regression test suite (run with ctest)" OFF)
endif()
if(NS_BUILD_TESTS AND PROJECT_IS_TOP_LEVEL)
    enable_testing()     # before libneurosuite-core, so its tests register too
endif()
get_filename_component(NDM_NS3_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT TARGET Neurosuite::core)
    add_subdirectory("${NDM_NS3_ROOT}/src/libneurosuite-core"
                     "${CMAKE_CURRENT_BINARY_DIR}/libneurosuite-core")
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(scripts)
add_subdirectory(descriptions)
add_subdirectory(src)

# Script tests (opt-in, run with ctest).  After src/, so the binaries they
# cross-check are known targets.
if(NS_BUILD_TESTS)
    add_subdirectory(test)
endif()
# matlab/ holds standalone MATLAB helpers; it is not part of the CMake build.
