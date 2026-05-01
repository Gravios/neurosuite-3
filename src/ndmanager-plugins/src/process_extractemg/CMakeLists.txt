cmake_minimum_required(VERSION 3.16)

add_executable(process_extractemg
    process_extractemg.cpp
)
target_compile_features(process_extractemg PRIVATE cxx_std_17)

# C++ math symbols (M_PI in <cmath>) require _GNU_SOURCE on glibc and an
# explicit USE_MATH_DEFINES on Windows.  Both expand to no-ops elsewhere.
target_compile_definitions(process_extractemg PRIVATE
    _USE_MATH_DEFINES
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_extractemg PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_extractemg: OpenMP enabled")
else()
    message(STATUS "process_extractemg: OpenMP not found, single-threaded")
endif()

install(TARGETS process_extractemg
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

add_manpage(process_extractemg)
