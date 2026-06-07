# =============================================================================
# Build-system regression checks (static — no toolchain/configure needed).
# Registered as a ctest test; runs the cmake_invariants.cmake script which
# asserts the build-system invariants from the CMake audit.
# =============================================================================
add_test(NAME build_invariants
    COMMAND "${CMAKE_COMMAND}"
            -DREPO=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake_invariants.cmake")
set_tests_properties(build_invariants PROPERTIES
    FAIL_REGULAR_EXPRESSION "FAIL;regressed")
