include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES     86 89 100 120)
    add_executable(process_medianthreshold
        process_medianthreshold.cpp
        process_medianthreshold_cuda.cu)
    target_compile_definitions(process_medianthreshold PRIVATE USE_CUDA)
    set_target_properties(process_medianthreshold PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON)
    message(STATUS "process_medianthreshold: CUDA build (${CMAKE_CUDA_ARCHITECTURES})")
elseif(TARGET Neurosuite::metal)
    # Apple GPU (Metal), doc/design/metal-backend.md.  The kernel source is
    # embedded and compiled at run time; with no Metal device at run time the
    # program uses its CPU path.
    add_executable(process_medianthreshold
        process_medianthreshold.cpp
        process_medianthreshold_metal.cpp)
    ns_metal_embed(process_medianthreshold medianthreshold_kernels process_medianthreshold.metal)
    target_compile_definitions(process_medianthreshold PRIVATE USE_METAL)
    target_link_libraries(process_medianthreshold PRIVATE Neurosuite::metal)
    message(STATUS "process_medianthreshold: Metal build")
else()
    add_executable(process_medianthreshold process_medianthreshold.cpp)
    message(STATUS "process_medianthreshold: CPU build")
endif()
target_compile_features(process_medianthreshold PRIVATE cxx_std_20)
install(TARGETS process_medianthreshold DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_medianthreshold)

# The Metal path against the CPU path's median, exactly (SKIPPED without a
# Metal device).
if(NS_BUILD_TESTS AND TARGET Neurosuite::metal AND NOT CMAKE_CUDA_COMPILER)
    add_executable(process_medianthreshold_metal_test
        process_medianthreshold_metal_test.cpp
        process_medianthreshold_metal.cpp)
    ns_metal_embed(process_medianthreshold_metal_test medianthreshold_kernels process_medianthreshold.metal)
    target_compile_features(process_medianthreshold_metal_test PRIVATE cxx_std_20)
    target_link_libraries(process_medianthreshold_metal_test PRIVATE Neurosuite::metal)
    add_test(NAME ndm_test_medianthreshold_metal COMMAND process_medianthreshold_metal_test)
    set_tests_properties(ndm_test_medianthreshold_metal PROPERTIES
        SKIP_REGULAR_EXPRESSION "SKIPPED" LABELS "ndmanager-plugins")
endif()
