cmake_minimum_required(VERSION 3.22...3.31)
project(libklustersshared VERSION 2.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Qt automoc/uic/rcc
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(yaml-cpp REQUIRED)

# ---------------------------------------------------------------------------
# Install layout
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Linker hardening (GCC/Clang on non-Apple Linux)
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    add_link_options(
        -Wl,--fatal-warnings
        -Wl,--no-undefined)
endif()

# ---------------------------------------------------------------------------
# Symbol visibility — keeps the public API surface explicit
# ---------------------------------------------------------------------------
set(CMAKE_CXX_VISIBILITY_PRESET     hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# ---------------------------------------------------------------------------
# Package config files
# ---------------------------------------------------------------------------
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LibKlustersSharedConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibKlustersShared")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfigVersion.cmake"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

# ---------------------------------------------------------------------------
# Build the library
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Install cmake package files
# ---------------------------------------------------------------------------
install(EXPORT LibKlustersSharedTargets
    NAMESPACE   KlustersShared::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibKlustersShared"
    FILE        LibKlustersSharedTargets.cmake)

# Build-tree export is driven from the top-level monorepo CMakeLists.txt
# (after add_subdirectory) where the export set is guaranteed to be fully
# populated.  See the ns_add_subdir(libklustersshared …) call there.

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibKlustersShared"
    COMPONENT   Devel)

include(CPack)
