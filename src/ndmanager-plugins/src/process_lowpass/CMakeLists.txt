find_package(OpenMP)

# process_lowpass is a pure C++/STL CLI tool (a linear-phase FIR low-pass).
# The FIR is cheap relative to the median high-pass, so — unlike
# process_medianfilter — there is no CUDA path; the OpenMP CPU path is enough.
add_executable(process_lowpass process_lowpass.cpp)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_lowpass PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_lowpass: CPU+OpenMP build")
else()
    message(STATUS "process_lowpass: CPU single-threaded build")
endif()
target_compile_features(process_lowpass PRIVATE cxx_std_17)
install(TARGETS process_lowpass DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_lowpass)
