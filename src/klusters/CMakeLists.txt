cmake_minimum_required(VERSION 3.16)
project(klusters VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

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
