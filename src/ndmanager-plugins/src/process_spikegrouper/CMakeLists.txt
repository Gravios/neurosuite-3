include(CheckLanguage)
check_language(CUDA)
find_package(OpenMP)

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES     86 89 100 120)
    add_executable(process_spikegrouper
        process_spikegrouper.cpp
        process_spikegrouper_cuda.cu)
    target_compile_definitions(process_spikegrouper PRIVATE USE_CUDA)
    set_target_properties(process_spikegrouper PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(process_spikegrouper PRIVATE OpenMP::OpenMP_CXX)
    endif()
    message(STATUS "process_spikegrouper: CUDA build (${CMAKE_CUDA_ARCHITECTURES})")
elseif(TARGET Neurosuite::metal)
    # Apple GPU (Metal), doc/design/metal-backend.md: noise medians and
    # coincidence counts on the GPU; no device at run time: the CPU path.
    add_executable(process_spikegrouper
        process_spikegrouper.cpp
        process_spikegrouper_metal.cpp)
    ns_metal_embed(process_spikegrouper spikegrouper_kernels process_spikegrouper.metal)
    target_compile_definitions(process_spikegrouper PRIVATE USE_METAL)
    target_link_libraries(process_spikegrouper PRIVATE Neurosuite::metal)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(process_spikegrouper PRIVATE OpenMP::OpenMP_CXX)
    endif()
    message(STATUS "process_spikegrouper: Metal build")
else()
    add_executable(process_spikegrouper process_spikegrouper.cpp)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(process_spikegrouper PRIVATE OpenMP::OpenMP_CXX)
        message(STATUS "process_spikegrouper: CPU+OpenMP build")
    else()
        message(STATUS "process_spikegrouper: CPU single-threaded build")
    endif()
endif()
target_compile_features(process_spikegrouper PRIVATE cxx_std_20)
install(TARGETS process_spikegrouper DESTINATION "${CMAKE_INSTALL_BINDIR}")

# The Metal helpers against the CPU path's rules, exactly (SKIPPED without a
# Metal device).
if(NS_BUILD_TESTS AND TARGET Neurosuite::metal AND NOT CMAKE_CUDA_COMPILER)
    add_executable(process_spikegrouper_metal_test
        process_spikegrouper_metal_test.cpp
        process_spikegrouper_metal.cpp)
    ns_metal_embed(process_spikegrouper_metal_test spikegrouper_kernels process_spikegrouper.metal)
    target_compile_features(process_spikegrouper_metal_test PRIVATE cxx_std_20)
    target_link_libraries(process_spikegrouper_metal_test PRIVATE Neurosuite::metal)
    add_test(NAME ndm_test_spikegrouper_metal COMMAND process_spikegrouper_metal_test)
    set_tests_properties(ndm_test_spikegrouper_metal PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED" LABELS "ndmanager-plugins")
endif()
