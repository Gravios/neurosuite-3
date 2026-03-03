cmake_minimum_required(VERSION 3.16)

add_executable(process_extractspikes_sdiff
    process_extractspikes_sdiff.cpp
)

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
