# NdmManPage.cmake
# Provides add_manpage(<name>) — builds and installs a gzipped man page from
# <name>.docbook in the caller's source directory via xsltproc + DocBook XSL.
# Silently skips if the .docbook file or the stylesheet is absent.
#
# And add_md_manpage(<name> <docdir>) — the man page of a command from its page
# in the command reference (<docdir>/*.md) via md2man.py (Python 3, standard
# library only).  Skipped, with a note, when Python 3, the folder or the
# command's page is missing.
#
# Every page gets the same header and footer: "<project> <version>" and
# "NDManager Plugins" (the DocBook pages through ndm-manpage-meta.xml, written
# here; without it xsltproc prints "[FIXME: source]" / "[FIXME: manual]").

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

# Header/footer of every page: the top-level project (neurosuite-3 3.1.0 in a
# full build, ndmanager-plugins <version> when configured on its own).
set(NDM_MAN_SOURCE "${CMAKE_PROJECT_NAME} ${CMAKE_PROJECT_VERSION}")
string(STRIP "${NDM_MAN_SOURCE}" NDM_MAN_SOURCE)
set(NDM_MAN_MANUAL "NDManager Plugins")
set(_NDM_MAN_META "${CMAKE_BINARY_DIR}/ndm-manpage-meta.xml")
file(WRITE "${_NDM_MAN_META}"
     "<m><source>${CMAKE_PROJECT_NAME}</source><version>${CMAKE_PROJECT_VERSION}</version>"
     "<manual>${NDM_MAN_MANUAL}</manual></m>\n")

find_package(Python3 COMPONENTS Interpreter QUIET)
set(_NDM_MD2MAN "${CMAKE_CURRENT_LIST_DIR}/md2man.py")

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
        DEPENDS "${_docbook}" "${_NDM_MAN_META}"
        COMMAND xsltproc --nonet --novalid
                --param refentry.source.name.profile.enabled 1
                --stringparam refentry.source.name.profile "document('${_NDM_MAN_META}')/m/source"
                --param refentry.version.profile.enabled 1
                --stringparam refentry.version.profile "document('${_NDM_MAN_META}')/m/version"
                --param refentry.manual.profile.enabled 1
                --stringparam refentry.manual.profile "document('${_NDM_MAN_META}')/m/manual"
                "${_NDM_DOCBOOK_XSL}" "${_docbook}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating ${_man}"
        VERBATIM)

    add_custom_command(
        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/${_gz}"
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${_man}"
        COMMAND gzip -n -f -k "${_man}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Compressing ${_gz}"
        VERBATIM)

    add_custom_target("man-${name}" ALL
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${_gz}")

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_gz}"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endfunction()

function(add_md_manpage name docdir)
    if(NOT Python3_Interpreter_FOUND OR NOT IS_DIRECTORY "${docdir}")
        return()
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_NDM_MD2MAN}" --docdir "${docdir}" --find "${name}"
        OUTPUT_VARIABLE _page OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(STATUS "man page: ${name} is not in ${docdir}; no man page")
        return()
    endif()
    # The page it is on (re-found when the folder's pages change: CONFIGURE_DEPENDS).
    file(GLOB _pages CONFIGURE_DEPENDS "${docdir}/*.md")

    set(_man "${name}.1")
    set(_gz  "${_man}.gz")
    add_custom_command(
        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/${_man}"
        DEPENDS "${_page}" "${_NDM_MD2MAN}"
        COMMAND "${Python3_EXECUTABLE}" "${_NDM_MD2MAN}" --docdir "${docdir}" --name "${name}"
                --source "${NDM_MAN_SOURCE}" --manual "${NDM_MAN_MANUAL}"
                -o "${CMAKE_CURRENT_BINARY_DIR}/${_man}"
        COMMENT "Generating ${_man} from the command reference"
        VERBATIM)
    add_custom_command(
        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/${_gz}"
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${_man}"
        COMMAND gzip -n -f -k "${_man}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Compressing ${_gz}"
        VERBATIM)
    add_custom_target("man-${name}" ALL
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${_gz}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_gz}"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
endfunction()
