cmake_minimum_required(VERSION 3.16)

add_executable(process_reextractspikes_stderiv
    process_reextractspikes_stderiv.cpp
)
target_compile_features(process_reextractspikes_stderiv PRIVATE cxx_std_17)
# Pull in the shared klustersshared headers (sdiff_pairs.h is header-only, so no
# extra source is compiled — only the include directory is needed).
target_include_directories(process_reextractspikes_stderiv
    PRIVATE "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_reextractspikes_stderiv PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_reextractspikes_stderiv: OpenMP enabled")
else()
    message(STATUS "process_reextractspikes_stderiv: OpenMP not found, single-threaded")
endif()

install(TARGETS process_reextractspikes_stderiv
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
add_manpage(process_reextractspikes_stderiv)
