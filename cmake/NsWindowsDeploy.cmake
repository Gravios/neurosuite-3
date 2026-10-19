# NsWindowsDeploy.cmake — on Windows, `cmake --install` gives a folder that runs.
#
# Klusters, NeuroScope and NDManager need, next to their .exe:
#   - the project's own DLLs (neurosuite-core, neurosuite-qt): installed to bin
#     by their own install rules;
#   - Qt's DLLs and plugins (platforms/qwindows.dll, styles, image formats):
#     Qt's windeployqt copies them;
#   - the other DLLs they link: yaml-cpp from vcpkg, and with the Intel oneAPI
#     compiler its runtime (libmmd, svml_dispmd, libiomp5md, sycl8, ...);
#   - for a SYCL build, the Unified Runtime adapters (ur_*.dll), which the SYCL
#     runtime loads by name at run time, so no dependency scan sees them.
# The ndmanager-plugins programs (process_*.exe, xpathReader.exe) are installed
# to the same bin directory and need no Qt, but do link vcpkg's GSL, libxml2
# and yaml-cpp; their DLLs are copied the same way.
#
# ns_windows_deploy() adds one install step that does all of this.  It is a
# no-op on other platforms and with -DNS_DEPLOY_RUNTIME=OFF.  A missing tool or
# DLL is reported as a warning; it never fails the install.  The work happens in
# NsWindowsDeployInstall.cmake at install time.
#
# SPDX-License-Identifier: GPL-3.0-or-later
include_guard(GLOBAL)

option(NS_DEPLOY_RUNTIME
    "Windows: copy the Qt, vcpkg and oneAPI runtime DLLs next to the installed programs"
    ON)

function(ns_windows_deploy)
    if(NOT WIN32 OR NOT NS_DEPLOY_RUNTIME)
        return()
    endif()

    set(_exes "")
    foreach(_t IN ITEMS klusters neuroscope ndmanager)
        if(TARGET ${_t})
            list(APPEND _exes "${_t}")
        endif()
    endforeach()
    set(_plugins OFF)
    if(TARGET process_butterworth)
        set(_plugins ON)
    endif()
    if(NOT _exes AND NOT _plugins)
        return()
    endif()

    # Qt's deployment tool, beside qmake / moc in Qt's bin directory.
    find_program(NS_WINDEPLOYQT windeployqt
        HINTS "${Qt6_DIR}/../../../bin" "${QT6_INSTALL_PREFIX}/bin"
        DOC "Qt's windeployqt, run on the installed programs")

    # Where the programs' non-Qt DLLs live.
    set(_dirs "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND _dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    set(_oneapi_dirs "")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
        get_filename_component(_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        list(APPEND _oneapi_dirs "${_cxx_bin}")
        if(DEFINED ENV{CMPLR_ROOT})
            file(TO_CMAKE_PATH "$ENV{CMPLR_ROOT}/bin" _cmplr_bin)
            list(APPEND _oneapi_dirs "${_cmplr_bin}")
        endif()
        list(REMOVE_DUPLICATES _oneapi_dirs)
        list(APPEND _dirs ${_oneapi_dirs})
    endif()

    set(_sycl OFF)
    if(USE_SYCL AND TARGET klusters)
        set(_sycl ON)
    endif()

    # Variables are baked into the install script; ${CMAKE_INSTALL_PREFIX} and
    # the configuration are resolved when `cmake --install` runs.
    install(CODE "
        set(_ns_bindir      \"${CMAKE_INSTALL_BINDIR}\")
        set(_ns_exes        \"${_exes}\")
        set(_ns_dirs        \"${_dirs}\")
        set(_ns_oneapi_dirs \"${_oneapi_dirs}\")
        set(_ns_windeployqt \"${NS_WINDEPLOYQT}\")
        set(_ns_wdq_mode    \"$<IF:$<CONFIG:Debug>,--debug,--release>\")
        set(_ns_sycl        ${_sycl})
        set(_ns_plugins     ${_plugins})
        include(\"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/NsWindowsDeployInstall.cmake\")
    ")
    if(_plugins)
        list(APPEND _exes "ndmanager-plugins")
    endif()
    message(STATUS "neurosuite-3: install also deploys the runtime DLLs "
                   "(${_exes}; -DNS_DEPLOY_RUNTIME=OFF to skip)")
endfunction()
