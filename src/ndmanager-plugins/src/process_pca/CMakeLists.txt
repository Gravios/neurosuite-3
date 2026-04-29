find_package(GSL REQUIRED)
find_package(OpenMP)

# Path to the canonical klustersshared headers and the progressbar
# implementation.  We reference the source files directly rather than
# linking libklustersshared because process_pca is a pure CLI tool with
# no Qt dependency, and libklustersshared is a Qt library — linking it
# would drag in Qt at runtime for a tool that doesn't need it.
set(_klustersshared_dir
    "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")

add_executable(process_pca
    process_pca.cpp
    "${_klustersshared_dir}/progressbar.cpp")
target_include_directories(process_pca
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_klustersshared_dir}")
target_compile_features(process_pca PRIVATE cxx_std_17)
target_link_libraries(process_pca PRIVATE GSL::gsl GSL::gslcblas)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_pca PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_pca: OpenMP enabled")
else()
    message(STATUS "process_pca: OpenMP not found, single-threaded")
endif()
install(TARGETS process_pca DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_pca)
