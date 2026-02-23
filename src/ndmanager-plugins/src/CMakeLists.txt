project(ndmanager-plugins-src)
cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# add_manpage(<program>)
#
# Builds and installs a gzipped man page from <program>.docbook using
# xsltproc + the DocBook XSL manpages stylesheet.  No-op if the docbook
# file is absent or the stylesheet is not installed.
# ---------------------------------------------------------------------------
function(add_manpage program)
    set(_docbook "${CMAKE_CURRENT_SOURCE_DIR}/${program}.docbook")
    if(NOT EXISTS "${_docbook}")
        return()
    endif()

    # Locate the DocBook XSL manpages stylesheet
    file(GLOB_RECURSE _xsl_candidates /usr/share/docbook.xsl)
    set(_xsl "")
    foreach(_f ${_xsl_candidates})
        string(FIND "${_f}" manpages _found)
        if(NOT _found EQUAL -1)
            set(_xsl "${_f}")
        endif()
    endforeach()
    if(NOT _xsl)
        message(STATUS "DocBook XSL not found — skipping man page for ${program}")
        return()
    endif()

    set(_man  "${program}.1")
    set(_gzip "${_man}.gz")

    add_custom_command(OUTPUT "${_man}"
        DEPENDS "${_docbook}"
        COMMAND xsltproc --nonet --novalid "${_xsl}" "${_docbook}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_custom_command(OUTPUT "${_gzip}"
        DEPENDS "${_man}"
        COMMAND gzip -f "${_man}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_custom_target(man-${program} ALL DEPENDS "${_gzip}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${_gzip}" DESTINATION man/man1)
endfunction()

add_subdirectory(process)
add_subdirectory(process_extractchannels)
add_subdirectory(process_extractleds)
add_subdirectory(process_extractspikes)
add_subdirectory(process_medianfilter)
add_subdirectory(process_medianthreshold)
add_subdirectory(process_merge)
add_subdirectory(process_nlxconvert)
add_subdirectory(process_pca)
add_subdirectory(process_removeartefacts)
add_subdirectory(process_resample)
add_subdirectory(process_smrconvert)
add_subdirectory(xpathReader)
add_subdirectory(process_mergefeatures)
