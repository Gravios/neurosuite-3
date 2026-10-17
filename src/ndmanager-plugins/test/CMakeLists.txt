# =============================================================================
# ndmanager-plugins script tests (NS_BUILD_TESTS=ON).
#
# Every test_*.py and test_*.sh here is a self-contained program that exits
# non-zero on failure and prints "SKIPPED" when something it needs is missing.
# Each one is registered as ndm_test_<name>.  The ones that cross-check C++
# binaries get their paths from the build, so under ctest they test this build's
# binaries, not whatever is on PATH.
#
# test_custody_conformance.{py,sh} are registered by libneurosuite-core
# (custody_conformance_python / _bash) next to the vector table they check.
# =============================================================================
find_package(Python3 COMPONENTS Interpreter)
find_program(NDM_TEST_BASH bash)

file(GLOB _ndm_tests CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/test_*.py"
     "${CMAKE_CURRENT_SOURCE_DIR}/test_*.sh")
list(FILTER _ndm_tests EXCLUDE REGEX "test_custody_conformance\\.(py|sh)$")

# test script -> the binaries it cross-checks (environment variable, target pairs)
set(_ndm_bin_test_adapt_format PROCESS_REASSIGNSPIKES_BIN process_reassignspikes)
set(_ndm_bin_test_plx2dat      PROCESS_PLXCONVERT_BIN     process_plxconvert)
set(_ndm_bin_test_subset_pca   PROCESS_PCA_BIN            process_pca)
set(_ndm_bin_test_extract_alignment
    PROCESS_EXTRACTSPIKES_BIN         process_extractspikes
    PROCESS_EXTRACTSPIKES_SDIFF_BIN   process_extractspikes_sdiff
    PROCESS_EXTRACTSPIKES_STDERIV_BIN process_extractspikes_stderiv)

foreach(_t IN LISTS _ndm_tests)
    get_filename_component(_name "${_t}" NAME_WE)
    get_filename_component(_ext  "${_t}" EXT)
    string(REGEX REPLACE "^test_" "" _short "${_name}")
    if(_ext STREQUAL ".py")
        if(NOT Python3_Interpreter_FOUND)
            continue()
        endif()
        set(_cmd ${Python3_EXECUTABLE} "${_t}")
    else()
        if(NOT NDM_TEST_BASH)
            continue()
        endif()
        set(_cmd ${NDM_TEST_BASH} "${_t}")
    endif()
    add_test(NAME ndm_test_${_short} COMMAND ${_cmd}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(ndm_test_${_short} PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED"
        LABELS "ndmanager-plugins")
    if(DEFINED _ndm_bin_${_name})
        set(_pairs ${_ndm_bin_${_name}})          # variable, target, variable, target ...
        while(_pairs)
            list(POP_FRONT _pairs _var _target)
            if(TARGET ${_target})
                set_property(TEST ndm_test_${_short} APPEND PROPERTY
                    ENVIRONMENT "${_var}=$<TARGET_FILE:${_target}>")
            endif()
        endwhile()
    endif()
endforeach()
