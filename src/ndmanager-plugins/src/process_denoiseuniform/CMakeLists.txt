find_package(OpenMP)

add_executable(process_denoiseuniform
    process_denoiseuniform.cpp)
target_compile_features(process_denoiseuniform PRIVATE cxx_std_17)

if(OpenMP_CXX_FOUND)
    target_link_libraries(process_denoiseuniform PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_denoiseuniform: CPU+OpenMP build")
else()
    message(STATUS "process_denoiseuniform: CPU single-threaded build")
endif()

install(TARGETS process_denoiseuniform DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_denoiseuniform)
