project(ndmanager-plugins-src-process_extractspikes)
cmake_minimum_required(VERSION 3.16)

set(program process_extractspikes)
add_executable(${program} process_extractspikes.cpp)
target_compile_features(${program} PRIVATE cxx_std_17)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(${program} PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "${program}: OpenMP enabled")
else()
    message(STATUS "${program}: OpenMP not found, single-threaded build")
endif()

install(TARGETS ${program} DESTINATION bin)
add_manpage(${program})
