cmake_minimum_required(VERSION 3.22...3.31)
project(nphys-data VERSION 2.0.0 LANGUAGES NONE)

include(GNUInstallDirs)

# Only configure CPack when nphys-data is built standalone.  In the monorepo it
# is add_subdirectory()'d, where a per-subproject CPack config is inappropriate
# (packaging, if any, is owned by the top-level build).
if(PROJECT_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME                "nphys-data")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MIME types and icons for Klusters, Neuroscope and NDManager")
    set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VERSION_MAJOR       "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR       "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH       "${PROJECT_VERSION_PATCH}")
    include(CPack)
endif()

add_subdirectory(src)
