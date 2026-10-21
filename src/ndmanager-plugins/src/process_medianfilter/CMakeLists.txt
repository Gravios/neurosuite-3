include(CheckLanguage)
check_language(CUDA)
find_package(OpenMP)

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES     86 89 100 120)
    add_executable(process_medianfilter
        process_medianfilter.cpp
        process_medianfilter_cuda.cu)
    target_compile_definitions(process_medianfilter PRIVATE USE_CUDA)
    set_target_properties(process_medianfilter PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(process_medianfilter PRIVATE OpenMP::OpenMP_CXX)
    endif()
    message(STATUS "process_medianfilter: CUDA build (${CMAKE_CUDA_ARCHITECTURES})")
elseif(TARGET Neurosuite::metal)
    # Apple GPU (Metal), doc/design/metal-backend.md: the kernel source is
    # embedded and compiled at run time; with no Metal device the CPU path runs.
    add_executable(process_medianfilter
        process_medianfilter.cpp
        process_medianfilter_metal.cpp)
    ns_metal_embed(process_medianfilter medianfilter_kernels process_medianfilter.metal)
    target_compile_definitions(process_medianfilter PRIVATE USE_METAL)
    target_link_libraries(process_medianfilter PRIVATE Neurosuite::metal)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(process_medianfilter PRIVATE OpenMP::OpenMP_CXX)
    endif()
    message(STATUS "process_medianfilter: Metal build")
else()
    add_executable(process_medianfilter process_medianfilter.cpp)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(process_medianfilter PRIVATE OpenMP::OpenMP_CXX)
        message(STATUS "process_medianfilter: CPU+OpenMP build")
    else()
        message(STATUS "process_medianfilter: CPU single-threaded build")
    endif()
endif()
# Stage progress bar: klustersshared's Qt-free progressbar source, compiled in
# as the other CLI plugins do.
set(_klustersshared_dir "${NDM_NS3_ROOT}/src/libklustersshared/src/klustersshared")
target_sources(process_medianfilter PRIVATE "${_klustersshared_dir}/progressbar.cpp")
target_include_directories(process_medianfilter PRIVATE "${_klustersshared_dir}")
target_compile_features(process_medianfilter PRIVATE cxx_std_20)
install(TARGETS process_medianfilter DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_medianfilter)
