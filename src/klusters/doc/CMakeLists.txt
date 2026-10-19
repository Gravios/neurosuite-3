# ---------------------------------------------------------------------------
# doc — the klusters handbook for Help ▸ Handbook (F1)
#
# The handbook is doc/klusters/ (README.md cluster-sorting.md hierarchical-clustering.md), the same Markdown the repository and the
# documentation site show; the in-app viewer renders it directly.  It is
# installed where the program's doc path points (HTML/en, a historical name).
# The old KDE DocBook handbook under en/ is no longer built or installed.
# ---------------------------------------------------------------------------
set(_ns_md_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../doc/klusters")
set(_ns_md "")
foreach(_f IN ITEMS README.md cluster-sorting.md hierarchical-clustering.md)
    if(EXISTS "${_ns_md_dir}/${_f}")
        list(APPEND _ns_md "${_ns_md_dir}/${_f}")
    endif()
endforeach()
if(_ns_md)
    install(FILES ${_ns_md} DESTINATION "share/doc/klusters/HTML/en")
else()
    message(STATUS "klusters: doc/klusters not found — the handbook (F1) is not installed")
endif()
