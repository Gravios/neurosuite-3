cmake_minimum_required(VERSION 3.16)

add_executable(process_drifttracker process_drifttracker.cpp)
target_compile_features(process_drifttracker PRIVATE cxx_std_17)

find_package(yaml-cpp REQUIRED)
target_link_libraries(process_drifttracker PRIVATE yaml-cpp)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_drifttracker PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_drifttracker: OpenMP enabled")
else()
    message(STATUS "process_drifttracker: OpenMP not found, single-threaded")
endif()

install(TARGETS process_drifttracker DESTINATION "${CMAKE_INSTALL_BINDIR}")
