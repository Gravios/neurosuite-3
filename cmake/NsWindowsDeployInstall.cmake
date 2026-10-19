# NsWindowsDeployInstall.cmake — install-time half of NsWindowsDeploy.cmake.
#
# Runs inside `cmake --install` with these variables set by the install rule:
#   _ns_bindir       install bin directory, relative to CMAKE_INSTALL_PREFIX
#   _ns_exes         program names (klusters;neuroscope;ndmanager)
#   _ns_dirs         directories holding the programs' non-Qt DLLs
#   _ns_oneapi_dirs  the oneAPI compiler's bin directories (may be empty)
#   _ns_windeployqt  path to windeployqt (may be empty / NOTFOUND)
#   _ns_wdq_mode     --release or --debug
#   _ns_sycl         ON for a SYCL build
#
# Nothing here fails the install: problems are warnings that name the fix.
#
# SPDX-License-Identifier: GPL-3.0-or-later

set(_ns_bin "${CMAKE_INSTALL_PREFIX}/${_ns_bindir}")

set(_ns_paths "")
foreach(_ns_e IN LISTS _ns_exes)
    if(EXISTS "${_ns_bin}/${_ns_e}.exe")
        list(APPEND _ns_paths "${_ns_bin}/${_ns_e}.exe")
    endif()
endforeach()
if(NOT _ns_paths)
    message(WARNING "neurosuite-3 deploy: no installed program found in ${_ns_bin}")
    return()
endif()

# ── 1. Qt: DLLs and plugins ───────────────────────────────────────────────────
if(_ns_windeployqt AND EXISTS "${_ns_windeployqt}")
    foreach(_ns_p IN LISTS _ns_paths)
        message(STATUS "neurosuite-3 deploy: windeployqt ${_ns_p}")
        execute_process(
            COMMAND "${_ns_windeployqt}" ${_ns_wdq_mode} --no-translations "${_ns_p}"
            RESULT_VARIABLE _ns_rc
            OUTPUT_VARIABLE _ns_out
            ERROR_VARIABLE  _ns_out)
        if(NOT _ns_rc EQUAL 0)
            message(WARNING "neurosuite-3 deploy: windeployqt failed for ${_ns_p} "
                            "(exit ${_ns_rc}):\n${_ns_out}")
        endif()
    endforeach()
else()
    message(WARNING "neurosuite-3 deploy: windeployqt not found — Qt DLLs not copied. "
                    "Run <Qt>\\bin\\windeployqt.exe on the installed programs.")
endif()

# ── 2. Other linked DLLs: vcpkg (yaml-cpp), oneAPI runtime, MSVC runtime ────
# Qt is left to windeployqt; Windows' own DLLs are never copied.  On Windows the
# scan reads the programs with dumpbin, which is on PATH in the Visual Studio
# and oneAPI command prompts; without it file(GET_RUNTIME_DEPENDENCIES) would
# stop the whole install, so say what to do and skip this step instead.
set(_ns_paths_scan ${_ns_paths})
if(CMAKE_HOST_WIN32 AND NOT CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND)
    find_program(_ns_dumpbin dumpbin)
    if(NOT _ns_dumpbin)
        message(WARNING "neurosuite-3 deploy: dumpbin not on PATH — yaml-cpp / oneAPI DLLs "
                        "not copied.  Run `cmake --install` from the Visual Studio or "
                        "oneAPI command prompt.")
        set(_ns_paths_scan "")
    endif()
endif()
if(_ns_paths_scan)
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${_ns_paths_scan}
    RESOLVED_DEPENDENCIES_VAR   _ns_resolved
    UNRESOLVED_DEPENDENCIES_VAR _ns_unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _ns_conflict
    DIRECTORIES "${_ns_bin}" ${_ns_dirs}
    PRE_EXCLUDE_REGEXES  "^api-ms-" "^ext-ms-" "^[Qq]t6"
    POST_EXCLUDE_REGEXES "[/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\]"
                         "[/\\\\][Ss][Yy][Ss][Ww][Oo][Ww]64[/\\\\]")
# A DLL found in two places: take the first, as the loader would.
foreach(_ns_name IN LISTS _ns_conflict_FILENAMES)
    list(GET _ns_conflict_${_ns_name} 0 _ns_first)
    list(APPEND _ns_resolved "${_ns_first}")
endforeach()

set(_ns_copied "")
foreach(_ns_dll IN LISTS _ns_resolved)
    get_filename_component(_ns_dir "${_ns_dll}" DIRECTORY)
    get_filename_component(_ns_name "${_ns_dll}" NAME)
    file(TO_CMAKE_PATH "${_ns_dir}" _ns_dir)
    file(TO_CMAKE_PATH "${_ns_bin}" _ns_bin_norm)
    if(NOT _ns_dir STREQUAL _ns_bin_norm)
        file(COPY "${_ns_dll}" DESTINATION "${_ns_bin}")
        list(APPEND _ns_copied "${_ns_name}")
    endif()
endforeach()
if(_ns_copied)
    list(JOIN _ns_copied ", " _ns_list)
    message(STATUS "neurosuite-3 deploy: copied ${_ns_list}")
endif()
if(_ns_unresolved)
    list(JOIN _ns_unresolved ", " _ns_list)
    message(WARNING "neurosuite-3 deploy: not found, so not copied: ${_ns_list}. "
                    "The programs will need them on PATH.")
endif()
endif()   # _ns_paths_scan

# ── 3. SYCL: Unified Runtime adapters, loaded by name at run time ────────────
if(_ns_sycl)
    set(_ns_ur "")
    foreach(_ns_d IN LISTS _ns_oneapi_dirs)
        file(GLOB _ns_found "${_ns_d}/ur_*.dll")
        list(APPEND _ns_ur ${_ns_found})
    endforeach()
    if(_ns_ur)
        file(COPY ${_ns_ur} DESTINATION "${_ns_bin}")
        message(STATUS "neurosuite-3 deploy: copied the SYCL runtime adapters (ur_*.dll)")
    else()
        message(WARNING "neurosuite-3 deploy: no SYCL runtime adapters (ur_*.dll) found; "
                        "run the programs from the oneAPI command prompt")
    endif()
endif()
