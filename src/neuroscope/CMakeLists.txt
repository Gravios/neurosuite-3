cmake_minimum_required(VERSION 3.21...3.31)
project(neuroscope VERSION 2.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Qt automation
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# Default build type
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "neuroscope: CMAKE_BUILD_TYPE defaulted to Release")
endif()

# ---------------------------------------------------------------------------
# Install layout
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml PrintSupport)

# LibKlustersShared exports KlustersShared::klustersshared via its installed
# CMake package config.  The cmake/modules/ directory is no longer needed.
# In a monorepo add_subdirectory build the alias target is already present.
if(NOT TARGET KlustersShared::klustersshared)
    find_package(LibKlustersShared 2.1.0 REQUIRED
        HINTS
            "${CMAKE_INSTALL_PREFIX}/lib/cmake/LibKlustersShared"
            /usr/local/lib/cmake/LibKlustersShared
            /usr/lib/cmake/LibKlustersShared)
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(doc)
