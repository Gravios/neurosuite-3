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
