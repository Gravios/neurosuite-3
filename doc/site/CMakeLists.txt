# ---------------------------------------------------------------------------
# doc/site — pandoc-based documentation regeneration (prototype)
#
# This is a STANDALONE, opt-in CMake project: it is intentionally NOT added to
# the top-level build (no ns_add_subdir), so the normal neurosuite-3 build is
# completely unaffected. Configure it on its own to regenerate the docs:
#
#     cmake -S doc/site -B build/docs
#     cmake --build build/docs --target handbook_inapp_html   # QTextBrowser HTML
#     cmake --build build/docs --target handbook_markdown      # MkDocs Markdown
#
# When you decide to cut over, the `handbook_inapp_html` output replaces the
# pre-built chunked HTML under src/neuroscope/doc/en/html/, and the install()
# in src/neuroscope/doc/CMakeLists.txt can point at it (see doc/site/README.md).
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(neurosuite3_docs NONE)

find_program(PANDOC_EXECUTABLE  pandoc)
find_program(XMLLINT_EXECUTABLE xmllint)
find_program(ICONV_EXECUTABLE   iconv)
find_program(BASH_EXECUTABLE    bash)

if(NOT PANDOC_EXECUTABLE OR NOT XMLLINT_EXECUTABLE OR NOT ICONV_EXECUTABLE OR NOT BASH_EXECUTABLE)
    message(WARNING
        "doc/site: pandoc / xmllint / iconv / bash not all found — the doc "
        "regeneration targets will not be available. Install them to use this.")
    return()
endif()

set(_gen "${CMAKE_CURRENT_SOURCE_DIR}/gen.sh")
set(_inapp "${CMAKE_CURRENT_BINARY_DIR}/inapp")

# Regenerate the single, self-contained, QTextBrowser-safe in-app handbook HTML
# (index.html + Images/) from the DocBook source.
add_custom_target(handbook_inapp_html
    COMMAND "${BASH_EXECUTABLE}" "${_gen}" "${_inapp}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating QTextBrowser-safe in-app handbook HTML -> ${_inapp}/index.html"
    VERBATIM USES_TERMINAL)

# gen.sh produces the MkDocs Markdown as a side effect; expose it under a name
# that says so (same script, so the two targets are not built in parallel).
add_custom_target(handbook_markdown
    COMMAND "${BASH_EXECUTABLE}" "${_gen}" "${_inapp}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating MkDocs handbook Markdown -> doc/site/docs/handbook/"
    VERBATIM USES_TERMINAL)
