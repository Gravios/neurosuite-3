find_package(GSL REQUIRED)
find_package(OpenMP)

# Reference the canonical klustersshared progressbar source directly.  No
# Qt dependency.
set(_klustersshared_dir
    "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")

add_executable(process_pca_multichannel
    process_pca_multichannel.cpp
    "${_klustersshared_dir}/progressbar.cpp")
target_include_directories(process_pca_multichannel
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_klustersshared_dir}")
target_compile_features(process_pca_multichannel PRIVATE cxx_std_17)
target_link_libraries(process_pca_multichannel PRIVATE GSL::gsl GSL::gslcblas)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_pca_multichannel PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_pca_multichannel: OpenMP enabled")
else()
    message(STATUS "process_pca_multichannel: OpenMP not found, single-threaded")
endif()
install(TARGETS process_pca_multichannel DESTINATION "${CMAKE_INSTALL_BINDIR}")
