cmake_minimum_required(VERSION 3.16)

add_executable(process_shadowcluster process_shadowcluster.cpp)
target_compile_features(process_shadowcluster PRIVATE cxx_std_17)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_shadowcluster PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_shadowcluster: OpenMP enabled")
else()
    message(STATUS "process_shadowcluster: OpenMP not found, single-threaded")
endif()

install(TARGETS process_shadowcluster DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_shadowcluster)
