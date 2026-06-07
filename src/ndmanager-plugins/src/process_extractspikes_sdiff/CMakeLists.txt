cmake_minimum_required(VERSION 3.16)

# Compile the canonical Qt-free progressbar.cpp (BlockProgress defrag-style
# stage display) as shared source — not linking the Qt libklustersshared,
# matching process_pca / process_extractspikes.
set(_klustersshared_dir
    "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")

add_executable(process_extractspikes_sdiff
    process_extractspikes_sdiff.cpp
    "${_klustersshared_dir}/progressbar.cpp"
)
target_include_directories(process_extractspikes_sdiff
    PRIVATE "${_klustersshared_dir}")

target_compile_features(process_extractspikes_sdiff PRIVATE cxx_std_17)

# Enable OpenMP if available (parallel group processing).
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_extractspikes_sdiff PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_extractspikes_sdiff: OpenMP enabled")
else()
    message(STATUS "process_extractspikes_sdiff: OpenMP not found, single-threaded")
endif()

install(TARGETS process_extractspikes_sdiff
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

add_manpage(process_extractspikes_sdiff)

# Opt into host-ISA vectorisation (-march=native) for this CPU-bound, hand-written
# numeric kernel.  ns_native_arch (cmake/ZenOptimizations.cmake) is CXX-only and
# gated on NS_ZEN_OPT, so device TUs are untouched and NS_ZEN_OPT=OFF stays
# portable.  Guarded so the plugin still builds if the module is absent.
if(TARGET ns_native_arch)
    target_link_libraries(process_extractspikes_sdiff PRIVATE ns_native_arch)
endif()
