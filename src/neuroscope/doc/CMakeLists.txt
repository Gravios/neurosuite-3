# ---------------------------------------------------------------------------
# doc — the NeuroScope handbook for Help ▸ Handbook (F1)
#
# en/handbook.html is generated from en/index.docbook by doc/site/gen.sh
# (pandoc; the same run regenerates the documentation site's handbook pages)
# and committed, so the build needs no DocBook tools.  It is installed as
# index.html beside the screenshots, where NEUROSCOPE_DOC_PATH points.
# ---------------------------------------------------------------------------
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/en/handbook.html"
    DESTINATION "share/doc/neuroscope/HTML/en"
    RENAME index.html)

file(GLOB _images "${CMAKE_CURRENT_SOURCE_DIR}/en/Images/*.png")
if(_images)
    install(FILES ${_images}
        DESTINATION "share/doc/neuroscope/HTML/en/Images")
endif()
