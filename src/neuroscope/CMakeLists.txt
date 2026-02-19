cmake_minimum_required(VERSION 3.16)
project(neuroscope VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Xml PrintSupport)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

find_package(LibKlustersShared 2.0.0 REQUIRED)

include_directories(
    ${QT_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LIBKLUSTERSSHARED_INCLUDE_DIR}
)

add_subdirectory(src)
