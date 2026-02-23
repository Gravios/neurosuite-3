project(ndmanager-plugins-src-process_pca)
cmake_minimum_required(VERSION 3.16)

set(program process_pca)

# progressbar and customtypes now live in libklustersshared; use them if installed,
# otherwise fall back to local copies (keeps the build self-contained).
find_package(LibKlustersShared QUIET
    PATHS /usr/local/lib/cmake/LibKlustersShared
          /usr/lib/cmake/LibKlustersShared)

if(LibKlustersShared_FOUND)
    add_executable(${program} process_pca.cpp)
    target_link_libraries(${program} PRIVATE klustersshared)
    message(STATUS "${program}: using progressbar from libklustersshared")
else()
    add_executable(${program} process_pca.cpp progressbar.cpp)
    target_include_directories(${program} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    message(STATUS "${program}: libklustersshared not found — using local progressbar")
endif()

target_compile_features(${program} PRIVATE cxx_std_17)

find_package(GSL REQUIRED)
find_package(OpenMP)

if(OpenMP_CXX_FOUND)
    target_link_libraries(${program} PRIVATE GSL::gsl GSL::gslcblas OpenMP::OpenMP_CXX)
    message(STATUS "${program}: OpenMP enabled")
else()
    target_link_libraries(${program} PRIVATE GSL::gsl GSL::gslcblas)
    message(STATUS "${program}: OpenMP not found, single-threaded build")
endif()

install(TARGETS ${program} DESTINATION bin)
add_manpage(${program})
