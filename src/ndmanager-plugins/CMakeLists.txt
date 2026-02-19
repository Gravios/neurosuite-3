cmake_minimum_required(VERSION 3.16)
project(ndmanager-plugins VERSION 1.4.14 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)

add_subdirectory(scripts)
add_subdirectory(descriptions)
add_subdirectory(src)
# Note: python/ and matlab/ subdirectories require PyQt5/PyQt6 and are not included here

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Plugins for NDManager")
set(CPACK_PACKAGE_NAME "ndmanager-plugins")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "4")
set(CPACK_PACKAGE_VERSION_PATCH "14")
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
include(CPack)
