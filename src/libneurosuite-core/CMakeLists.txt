cmake_minimum_required(VERSION 3.22...3.31)
project(libneurosuite-core VERSION 2.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# No Qt, no AUTOMOC — this library is deliberately Qt-free.

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
    "${CMAKE_CURRENT_SOURCE_DIR}/LibNeurosuiteCoreConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LibNeurosuiteCoreConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibNeurosuiteCore")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LibNeurosuiteCoreConfigVersion.cmake"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

# ---------------------------------------------------------------------------
# Build the library
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Install cmake package files
# ---------------------------------------------------------------------------
install(EXPORT LibNeurosuiteCoreTargets
    NAMESPACE   Neurosuite::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibNeurosuiteCore"
    FILE        LibNeurosuiteCoreTargets.cmake)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LibNeurosuiteCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LibNeurosuiteCoreConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibNeurosuiteCore"
    COMPONENT   Devel)
