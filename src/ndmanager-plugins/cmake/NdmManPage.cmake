# NdmManPage.cmake
# Provides add_manpage(<name>) — builds and installs a gzipped man page from
# <name>.docbook in the caller's source directory via xsltproc + DocBook XSL.
# Silently skips if the .docbook file or the stylesheet is absent.

# Locate the DocBook XSL manpages stylesheet once, cache the result.
if(NOT DEFINED _NDM_DOCBOOK_XSL)
    file(GLOB_RECURSE _NDM_DOCBOOK_XSL_CANDIDATES /usr/share/docbook.xsl)
    set(_NDM_DOCBOOK_XSL "")
    foreach(_f IN LISTS _NDM_DOCBOOK_XSL_CANDIDATES)
        if(_f MATCHES "manpages")
            set(_NDM_DOCBOOK_XSL "${_f}")
            break()
        endif()
    endforeach()
    if(_NDM_DOCBOOK_XSL)
        message(STATUS "DocBook XSL manpages stylesheet: ${_NDM_DOCBOOK_XSL}")
    else()
        message(STATUS "DocBook XSL not found — man pages will not be built")
    endif()
    set(_NDM_DOCBOOK_XSL "${_NDM_DOCBOOK_XSL}" CACHE INTERNAL "DocBook XSL path")
endif()

function(add_manpage name)
    set(_docbook "${CMAKE_CURRENT_SOURCE_DIR}/${name}.docbook")
    if(NOT EXISTS "${_docbook}")
        return()
    endif()
    if(NOT _NDM_DOCBOOK_XSL)
        return()
    endif()

    set(_man "${name}.1")
    set(_gz  "${_man}.gz")

    add_custom_command(
        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/${_man}"
        DEPENDS "${_docbook}"
        COMMAND xsltproc --nonet --novalid
                "${_NDM_DOCBOOK_XSL}" "${_docbook}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating ${_man}"
        VERBATIM)

    add_custom_command(
        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/${_gz}"
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${_man}"
        COMMAND gzip -f "${_man}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Compressing ${_gz}"
        VERBATIM)

    add_custom_target("man-${name}" ALL
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${_gz}")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_gz}"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endfunction()
