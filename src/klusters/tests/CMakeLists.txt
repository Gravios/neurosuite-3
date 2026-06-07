# =============================================================================
# klusters unit tests (regression suite)
#
# Header-only tests for the SpikeAssignment sparse-set store and the
# SelectionGrid2D spatial index.  Built only when NS_BUILD_TESTS=ON; run with
# `ctest`.  No Qt dependency — these exercise the Qt-free data structures.
# =============================================================================
set(_klu_src "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(_shared  "${CMAKE_CURRENT_SOURCE_DIR}/../../libklustersshared/src/klustersshared")

add_executable(klusters_test_selectiongrid2d selectiongrid2d_test.cpp)
add_executable(klusters_test_spikeassignment spikeassignment_test.cpp)

foreach(_t klusters_test_selectiongrid2d klusters_test_spikeassignment)
    target_compile_features(${_t} PRIVATE cxx_std_20)
    target_include_directories(${_t} PRIVATE "${_klu_src}" "${_shared}")
    add_test(NAME ${_t} COMMAND ${_t})
endforeach()
