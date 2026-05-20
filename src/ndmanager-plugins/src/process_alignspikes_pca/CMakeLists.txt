add_executable(process_alignspikes_pca process_alignspikes_pca.cpp)
target_compile_features(process_alignspikes_pca PRIVATE cxx_std_17)
target_link_libraries(process_alignspikes_pca PRIVATE m)

# OpenMP — used for parallel xcorr over spikes within a cluster.  Plugin
# falls back to serial execution if OpenMP is unavailable.
find_package(OpenMP COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_alignspikes_pca PRIVATE OpenMP::OpenMP_CXX)
endif()

install(TARGETS process_alignspikes_pca DESTINATION "${CMAKE_INSTALL_BINDIR}")
