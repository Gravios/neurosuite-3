# ---------------------------------------------------------------------------
# ns_refresh_desktop_caches()
#
# After everything else is installed, rebuild the MIME database and the
# hicolor icon cache of the prefix installed into, so the session/file types
# (nphys-data, neuroscope, ndmanager) and the application icons are known
# without a separate step.  This replaces the Debian-style postinst/postrm
# scripts the subprojects carried, which nothing ran and which always
# targeted /usr whatever the prefix.
#
# Skipped for a staged install (DESTDIR set): a package's own triggers do it
# on the target system.
#
# Call it once, after the last add_subdirectory()/ns_add_subdir(): with
# CMP0082 (CMake >= 3.14) install rules run in declaration order, so the
# refresh then follows every installed .xml and icon.  Repeated calls in one
# configure are ignored, so a subproject can call it when it is built on its
# own and the monorepo once for all.
# ---------------------------------------------------------------------------
include_guard(GLOBAL)

function(ns_refresh_desktop_caches)
    get_property(_done GLOBAL PROPERTY NS_DESKTOP_CACHES_REFRESH)
    if(_done)
        return()
    endif()
    set_property(GLOBAL PROPERTY NS_DESKTOP_CACHES_REFRESH TRUE)
    include(GNUInstallDirs)
    install(CODE "set(_ns_datadir \"${CMAKE_INSTALL_DATADIR}\")")
    install(CODE [[
      if("$ENV{DESTDIR}" STREQUAL "")
        find_program(_ns_update_mime update-mime-database)
        if(_ns_update_mime AND EXISTS "${CMAKE_INSTALL_PREFIX}/${_ns_datadir}/mime/packages")
          execute_process(COMMAND "${_ns_update_mime}"
                                  "${CMAKE_INSTALL_PREFIX}/${_ns_datadir}/mime"
                          OUTPUT_QUIET ERROR_QUIET)
        endif()
        # Only a theme with its index (the system hicolor) keeps a cache that
        # would hide newly installed icons.
        set(_ns_hicolor "${CMAKE_INSTALL_PREFIX}/${_ns_datadir}/icons/hicolor")
        find_program(_ns_update_icons gtk-update-icon-cache)
        if(_ns_update_icons AND EXISTS "${_ns_hicolor}/index.theme")
          execute_process(COMMAND "${_ns_update_icons}" -q "${_ns_hicolor}"
                          OUTPUT_QUIET ERROR_QUIET)
        endif()
      endif()
    ]])
endfunction()
