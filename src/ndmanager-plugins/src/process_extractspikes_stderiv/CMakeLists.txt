cmake_minimum_required(VERSION 3.16)

add_executable(process_extractspikes_stderiv
    process_extractspikes_stderiv.cpp
)
target_compile_features(process_extractspikes_stderiv PRIVATE cxx_std_17)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_extractspikes_stderiv PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_extractspikes_stderiv: OpenMP enabled")
else()
    message(STATUS "process_extractspikes_stderiv: OpenMP not found, single-threaded")
endif()

install(TARGETS process_extractspikes_stderiv
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

add_manpage(process_extractspikes_stderiv)
