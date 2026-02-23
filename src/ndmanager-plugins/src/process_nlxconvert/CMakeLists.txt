project(ndmanager-plugins-src-process_nlxconvert)
cmake_minimum_required(VERSION 3.16)

set(program process_nlxconvert)

set(sources
    ncsfilecollection.cpp
    ncsfile.cpp
    nevfile.cpp
    nlxerror.cpp
    nlxextractor.cpp
    nvtfile.cpp
    outputfile.cpp
    process_nlxconvert.cpp
    smifilecollection.cpp
    smifile.cpp)

#find_package(LibKlustersShared REQUIRED)

#find_package(LibKlustersShared QUIET
#    PATHS /usr/local/lib/cmake/LibKlustersShared
#          /usr/lib/cmake/LibKlustersShared)

# if(LibKlustersShared_FOUND)
#     add_executable(${program} ${sources})
#     target_link_libraries(${program} PRIVATE klustersshared)
#     message(STATUS "${program}: using progressbar from libklustersshared")
# else()
    add_executable(${program} ${sources} progressbar.cpp)
    target_include_directories(${program} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    message(STATUS "${program}: libklustersshared not found — using local progressbar")
#endif()

target_compile_features(${program} PRIVATE cxx_std_17)
install(TARGETS ${program} DESTINATION bin)
add_manpage(${program})
