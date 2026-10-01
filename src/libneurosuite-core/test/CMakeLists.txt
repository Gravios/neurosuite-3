# =============================================================================
# libneurosuite-core unit tests (regression suite)
#
# Self-contained programs (own main(), assert-based).  Built only when
# NS_BUILD_TESTS=ON; run with `ctest` from the build directory.
# =============================================================================
add_executable(core_test_custody_conformance custody_conformance_test.cpp)
target_compile_features(core_test_custody_conformance PRIVATE cxx_std_17)
target_include_directories(core_test_custody_conformance
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_custody_conformance
         COMMAND core_test_custody_conformance
                 "${CMAKE_CURRENT_SOURCE_DIR}/custody_vectors.tsv")

# .spk int16 waveform I/O + stage-tagged path composition round-trip.  Links the
# library (unlike the header-only custody test) because readSpk/writeSpk/stagePath
# live in neurofileio.cpp.
add_executable(core_test_neurofileio_spk neurofileio_spk_test.cpp)
target_compile_features(core_test_neurofileio_spk PRIVATE cxx_std_17)
target_include_directories(core_test_neurofileio_spk
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(core_test_neurofileio_spk PRIVATE Neurosuite::core)
add_test(NAME core_test_neurofileio_spk COMMAND core_test_neurofileio_spk)

# Two-component collision-apply engine (decollide.hpp) — header-only, so no link
# (mirrors the custody conformance test).  The noise-free linear case is exact.
add_executable(core_test_decollide decollide_test.cpp)
target_compile_features(core_test_decollide PRIVATE cxx_std_17)
target_include_directories(core_test_decollide
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_decollide COMMAND core_test_decollide)

# PCAE transform-method enum conformance.  Pins core::Method's integer values and
# its spatialOrder/hasTemporalDiff/methodTag helpers against the shared table; the
# fiber-kit Python mirror runs an identical copy so the cross-repo enum cannot drift.
add_executable(core_test_pca_method_conformance pca_method_conformance_test.cpp)
target_compile_features(core_test_pca_method_conformance PRIVATE cxx_std_17)
target_include_directories(core_test_pca_method_conformance
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_pca_method_conformance
         COMMAND core_test_pca_method_conformance
                 "${CMAKE_CURRENT_SOURCE_DIR}/pca_method_vectors.tsv")

# Run the Python mirror (ndm_resolve_io) against the SAME vectors, so the C++
# and Python implementations are kept in lock-step by `ctest`.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME custody_conformance_python
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/src/ndmanager-plugins/test/test_custody_conformance.py")
endif()

# Run the bash mirror (ndm_custody) against the SAME vectors, completing the
# three-language lock-step (C++ / Python / bash) enforced by `ctest`.
find_program(BASH_PROGRAM bash)
if(BASH_PROGRAM)
    add_test(NAME custody_conformance_bash
             COMMAND ${BASH_PROGRAM}
                     "${CMAKE_SOURCE_DIR}/src/ndmanager-plugins/test/test_custody_conformance.sh"
                     "${CMAKE_CURRENT_SOURCE_DIR}/custody_vectors.tsv")
endif()
