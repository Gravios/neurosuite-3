# =============================================================================
# klusters unit tests (regression suite)
#
# Header-only tests for the SpikeAssignment sparse-set store, the
# SelectionGrid2D spatial index, and the PCA row-projection kernel.  Built only when NS_BUILD_TESTS=ON; run with
# `ctest`.  No Qt dependency — these exercise the Qt-free data structures.
# =============================================================================
set(_klu_src "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(_shared  "${CMAKE_CURRENT_SOURCE_DIR}/../../libklustersshared/src/klustersshared")

add_executable(klusters_test_selectiongrid2d selectiongrid2d_test.cpp)
add_executable(klusters_test_spikeassignment spikeassignment_test.cpp)
add_executable(klusters_test_pcarowprojection pcarowprojection_test.cpp)

set(_core "${CMAKE_CURRENT_SOURCE_DIR}/../../libneurosuite-core/src")

foreach(_t klusters_test_selectiongrid2d klusters_test_spikeassignment
          klusters_test_pcarowprojection)
    target_compile_features(${_t} PRIVATE cxx_std_20)
    # _core carries the header-only PcaBasis the projection kernel reads; it is
    # an include path only, no library and no contract change.
    target_include_directories(${_t} PRIVATE "${_klu_src}" "${_shared}" "${_core}")
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()

# TemplateLineageStore — the Qt-free manual-lineage model (phase 5).  Unlike the
# header-only suites above it compiles the store source and LINKS Neurosuite::core
# (readWtl/writeWti/writeSpk + the renderLineage* engine live out of line there).
add_executable(klusters_test_templatelineagestore
    templatelineagestore_test.cpp "${_klu_src}/templatelineagestore.cpp")
target_compile_features(klusters_test_templatelineagestore PRIVATE cxx_std_20)
target_include_directories(klusters_test_templatelineagestore
    PRIVATE "${_klu_src}" "${_shared}" "${_core}")
target_link_libraries(klusters_test_templatelineagestore PRIVATE Neurosuite::core)
add_test(NAME klusters_test_templatelineagestore COMMAND klusters_test_templatelineagestore)

# TemplateClassStore — the Qt-free EAP template-class model (phase 4).  Same
# shape as the lineage-store test: compiles the store source and LINKS
# Neurosuite::core (readEap/readTcl/writeEap/writeTcl + the templateclass engine
# live out of line there).
add_executable(klusters_test_templateclassstore
    templateclassstore_test.cpp "${_klu_src}/templateclassstore.cpp")
target_compile_features(klusters_test_templateclassstore PRIVATE cxx_std_20)
target_include_directories(klusters_test_templateclassstore
    PRIVATE "${_klu_src}" "${_shared}" "${_core}")
target_link_libraries(klusters_test_templateclassstore PRIVATE Neurosuite::core)
add_test(NAME klusters_test_templateclassstore COMMAND klusters_test_templateclassstore)

# MatrixTemplateStrip geometry — the §11.5 marked-node template region's pixel
# <-> cell math and click hit-test (matrixtemplatecols.h).  Unlike the Qt-free
# suites above this one pulls QPointF/QColor, so it links Qt6 Core+Gui; guarded
# so a Qt-less configure just skips it.  The per-cell shade (computeShade) needs
# a live KlustersDoc and is not covered here.
find_package(Qt6 COMPONENTS Core Gui QUIET)
if(Qt6_FOUND)
    add_executable(klusters_test_matrixtemplatestrip matrixtemplatestrip_test.cpp)
    target_compile_features(klusters_test_matrixtemplatestrip PRIVATE cxx_std_20)
    target_include_directories(klusters_test_matrixtemplatestrip
        PRIVATE "${_klu_src}" "${_shared}" "${_core}")
    target_link_libraries(klusters_test_matrixtemplatestrip PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME klusters_test_matrixtemplatestrip COMMAND klusters_test_matrixtemplatestrip)

    # Input binding core (input-remapping plan §3/§4): Chord round-trip + the
    # BindingRegistry resolver/override/conflict logic.  Compiles the two Qt-light
    # module sources directly; links Qt6 Core+Gui (QString/Qt flags + QKeySequence
    # in Chord::displayString).  No widgets, no KlustersDoc.
    add_executable(klusters_test_bindingregistry
        bindingregistry_test.cpp
        "${_klu_src}/input/chord.cpp"
        "${_klu_src}/input/bindingregistry.cpp")
    target_compile_features(klusters_test_bindingregistry PRIVATE cxx_std_20)
    target_include_directories(klusters_test_bindingregistry
        PRIVATE "${_klu_src}" "${_shared}" "${_core}")
    target_link_libraries(klusters_test_bindingregistry PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME klusters_test_bindingregistry COMMAND klusters_test_bindingregistry)

    # Input dispatcher (input-remapping plan decision #1): chordFromEvent's QEvent ->
    # Chord mapping + dispatch() end-to-end.  Constructs QKeyEvent/QMouseEvent/
    # QWheelEvent without a QApplication (the view pointer is only stored, never
    # dereferenced), so it links Qt6 Core+Gui only — no widgets.
    add_executable(klusters_test_inputdispatcher
        inputdispatcher_test.cpp
        "${_klu_src}/input/chord.cpp"
        "${_klu_src}/input/bindingregistry.cpp"
        "${_klu_src}/input/inputdispatcher.cpp")
    target_compile_features(klusters_test_inputdispatcher PRIVATE cxx_std_20)
    target_include_directories(klusters_test_inputdispatcher
        PRIVATE "${_klu_src}" "${_shared}" "${_core}")
    target_link_libraries(klusters_test_inputdispatcher PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME klusters_test_inputdispatcher COMMAND klusters_test_inputdispatcher)
endif()
