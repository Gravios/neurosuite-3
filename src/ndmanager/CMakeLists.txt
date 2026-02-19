cmake_minimum_required(VERSION 3.16)
project(ndmanager VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml)

find_package(LibKlustersShared 2.0.0 REQUIRED)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config-ndmanager.h.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/config-ndmanager.h
)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LIBKLUSTERSSHARED_INCLUDE_DIR}
)

add_subdirectory(src)

# Install docs if present
file(GLOB _html ${CMAKE_CURRENT_SOURCE_DIR}/doc/en/html/*.html)
if(_html)
    install(FILES ${_html} DESTINATION share/doc/ndmanager/HTML/en)
endif()

set(CPACK_PACKAGE_NAME "ndmanager")
set(CPACK_PACKAGE_VERSION_MAJOR "2")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
include(CPack)
