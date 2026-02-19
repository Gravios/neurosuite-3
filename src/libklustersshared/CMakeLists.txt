cmake_minimum_required(VERSION 3.16)
project(libklustersshared VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(LIBKLUSTERSSHARED_VERSION       ${PROJECT_VERSION})
set(LIBKLUSTERSSHARED_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)")
set(EXEC_INSTALL_PREFIX  ${CMAKE_INSTALL_PREFIX} CACHE PATH "Base directory for executables and libraries")
set(BIN_INSTALL_DIR      "${EXEC_INSTALL_PREFIX}/bin"              CACHE PATH "Binary install dir")
set(LIB_INSTALL_DIR      "${EXEC_INSTALL_PREFIX}/lib${LIB_SUFFIX}" CACHE PATH "Library install dir")
set(INCLUDE_INSTALL_DIR  "${EXEC_INSTALL_PREFIX}/include/klustersshared" CACHE PATH "Include install dir")

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  if(NOT APPLE)
    set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--fatal-warnings -Wl,--no-undefined ${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS "-Wl,--fatal-warnings -Wl,--no-undefined ${CMAKE_MODULE_LINKER_FLAGS}")
  endif()
  add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)
endif()

configure_file(LibKlustersSharedConfig.cmake.in
               ${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfig.cmake @ONLY)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfigVersion.cmake
    VERSION ${LIBKLUSTERSSHARED_VERSION}
    COMPATIBILITY SameMajorVersion
)

add_definitions(-DKLUSTERSSHARED_BUILD_KLUSTERSSHARED_LIB)

add_subdirectory(src)

install(EXPORT LibKlustersSharedLibraryTargets
    DESTINATION ${LIB_INSTALL_DIR}/cmake/LibKlustersShared
    FILE LibKlustersSharedTargetsWithPrefix.cmake
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfigVersion.cmake
    DESTINATION ${LIB_INSTALL_DIR}/cmake/LibKlustersShared
    COMPONENT Devel
)
include(CPack)
