include(CheckLanguage)
check_language(CUDA)
find_package(OpenMP)

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD          17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES     86 89 100 120)
    add_executable(process_butterworth
        process_butterworth.cpp
        process_butterworth_cuda.cu)
    target_compile_definitions(process_butterworth PRIVATE USE_CUDA)
    message(STATUS "process_butterworth: CUDA build (${CMAKE_CUDA_ARCHITECTURES})")
elseif(TARGET Neurosuite::metal)
    # Apple GPU (Metal, single precision), doc/design/metal-backend.md; the
    # kernel is embedded and compiled at run time; no device: the CPU path.
    add_executable(process_butterworth
        process_butterworth.cpp
        process_butterworth_metal.cpp)
    ns_metal_embed(process_butterworth butterworth_kernels process_butterworth.metal)
    target_compile_definitions(process_butterworth PRIVATE USE_METAL)
    target_link_libraries(process_butterworth PRIVATE Neurosuite::metal)
    message(STATUS "process_butterworth: Metal build")
else()
    add_executable(process_butterworth process_butterworth.cpp)
    message(STATUS "process_butterworth: CPU build")
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_butterworth PRIVATE OpenMP::OpenMP_CXX)
endif()
# Stage progress bar: klustersshared's Qt-free progressbar source, compiled in
# as the other CLI plugins do.
set(_klustersshared_dir "${NDM_NS3_ROOT}/src/libklustersshared/src/klustersshared")
target_sources(process_butterworth PRIVATE "${_klustersshared_dir}/progressbar.cpp")
target_include_directories(process_butterworth PRIVATE "${_klustersshared_dir}")
target_compile_features(process_butterworth PRIVATE cxx_std_20)
install(TARGETS process_butterworth DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_butterworth)
