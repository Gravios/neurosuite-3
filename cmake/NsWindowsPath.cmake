# NsWindowsPath.cmake — on Windows, `cmake --install` puts the installed
# programs on the user's PATH, so klusters, neuroscope, ndmanager and the
# process_* programs run from any new prompt.
#
# The install step runs scripts/windows-path.ps1 -Add <prefix>/bin: the current
# user's PATH only (no administrator rights), entries kept unexpanded, and an
# earlier install to another prefix replaced rather than added to.  Off with
# -DNS_ADD_TO_PATH=OFF; skipped for a staged install (DESTDIR set).  Undo with
#   powershell -ExecutionPolicy Bypass -File scripts\windows-path.ps1 -Remove
# A failure is a warning; it never fails the install.
#
# SPDX-License-Identifier: GPL-3.0-or-later
include_guard(GLOBAL)

option(NS_ADD_TO_PATH
    "Windows: `cmake --install` adds the installed bin directory to the user's PATH"
    ON)

function(ns_windows_add_to_path)
    if(NOT WIN32 OR NOT NS_ADD_TO_PATH)
        return()
    endif()
    find_program(NS_POWERSHELL NAMES powershell pwsh
        DOC "PowerShell, which edits the user's PATH at install time")
    if(NOT NS_POWERSHELL)
        message(STATUS "neurosuite-3: PowerShell not found — install will not edit PATH")
        return()
    endif()
    set(_script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/windows-path.ps1")
    get_filename_component(_script "${_script}" ABSOLUTE)
    install(CODE "
        if(DEFINED ENV{DESTDIR} AND NOT \"\$ENV{DESTDIR}\" STREQUAL \"\")
            message(STATUS \"neurosuite-3: staged install (DESTDIR) — PATH left as it is\")
        else()
            file(TO_NATIVE_PATH \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\" _ns_path_bin)
            execute_process(
                COMMAND \"${NS_POWERSHELL}\" -NoProfile -ExecutionPolicy Bypass
                        -File \"${_script}\" -Add \"\${_ns_path_bin}\"
                RESULT_VARIABLE _ns_path_rc)
            if(NOT _ns_path_rc EQUAL 0)
                message(WARNING \"neurosuite-3: could not add \${_ns_path_bin} to the user PATH \"
                                \"(exit \${_ns_path_rc}); add it by hand, or run \"
                                \"scripts/windows-path.ps1 -Add \${_ns_path_bin}\")
            endif()
        endif()
    ")
    message(STATUS "neurosuite-3: install also adds its bin directory to the user PATH "
                   "(-DNS_ADD_TO_PATH=OFF to skip)")
endfunction()
