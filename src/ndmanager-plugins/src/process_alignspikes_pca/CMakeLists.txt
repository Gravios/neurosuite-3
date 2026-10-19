# sdiff_pairs.h (shared SDIFF_CUSTOM pattern parser) is header-only; take it
# as an include dir rather than linking the Qt libklustersshared, exactly as
# process_extractspikes_stderiv does.
set(_klustersshared_dir
    "${NDM_NS3_ROOT}/src/libklustersshared/src/klustersshared")
add_executable(process_alignspikes_pca process_alignspikes_pca.cpp)
target_include_directories(process_alignspikes_pca PRIVATE "${_klustersshared_dir}")
target_compile_features(process_alignspikes_pca PRIVATE cxx_std_20)
# libm: a separate library on Unix; part of the C runtime on Windows.
target_link_libraries(process_alignspikes_pca PRIVATE $<$<NOT:$<PLATFORM_ID:Windows>>:m>)

# OpenMP — used for parallel xcorr over spikes within a cluster.  Plugin
# falls back to serial execution if OpenMP is unavailable.
find_package(OpenMP COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_alignspikes_pca PRIVATE OpenMP::OpenMP_CXX)
endif()

install(TARGETS process_alignspikes_pca DESTINATION "${CMAKE_INSTALL_BINDIR}")
