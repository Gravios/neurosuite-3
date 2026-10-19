cmake_minimum_required(VERSION 3.16)

add_executable(process_reextractspikes process_reextractspikes.cpp)
# MappedFile (read-only map of the .dat / .fil; mmap or the Windows file mapping)
target_link_libraries(process_reextractspikes PRIVATE Neurosuite::core)
target_compile_features(process_reextractspikes PRIVATE cxx_std_20)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_reextractspikes PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_reextractspikes: OpenMP enabled")
else()
    message(STATUS "process_reextractspikes: OpenMP not found, single-threaded")
endif()

install(TARGETS process_reextractspikes DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_reextractspikes)
