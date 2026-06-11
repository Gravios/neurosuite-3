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

# Run the Python mirror (ndm_resolve_io) against the SAME vectors, so the C++
# and Python implementations are kept in lock-step by `ctest`.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME custody_conformance_python
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/src/ndmanager-plugins/test/test_custody_conformance.py")
endif()
