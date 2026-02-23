cmake_minimum_required(VERSION 3.21...3.31)
project(nphys-data VERSION 2.0.0)

include(GNUInstallDirs)

set(CPACK_PACKAGE_NAME                "nphys-data")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MIME types and icons for Klusters, Neuroscope and NDManager")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR       "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR       "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH       "${PROJECT_VERSION_PATCH}")
include(CPack)

add_subdirectory(src)
