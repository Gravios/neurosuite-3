# ---------------------------------------------------------------------------
# doc — install pre-built HTML documentation and images
# ---------------------------------------------------------------------------
file(GLOB _html "${CMAKE_CURRENT_SOURCE_DIR}/en/html/*.html")
if(_html)
    install(FILES ${_html}
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/HTML/en")
endif()

file(GLOB _images "${CMAKE_CURRENT_SOURCE_DIR}/en/Images/*.png")
if(_images)
    install(FILES ${_images}
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/HTML/en/Images")
endif()

# doc/en/ only needs to be included when regenerating docs from DocBook source,
# which requires KDE4 tooling no longer widely available.
# add_subdirectory(en)
