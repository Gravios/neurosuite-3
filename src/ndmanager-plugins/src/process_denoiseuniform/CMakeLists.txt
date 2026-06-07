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

# Opt into host-ISA vectorisation (-march=native) for this CPU-bound, hand-written
# numeric kernel.  ns_native_arch (cmake/ZenOptimizations.cmake) is CXX-only and
# gated on NS_ZEN_OPT, so device TUs are untouched and NS_ZEN_OPT=OFF stays
# portable.  Guarded so the plugin still builds if the module is absent.
if(TARGET ns_native_arch)
    target_link_libraries(process_denoiseuniform PRIVATE ns_native_arch)
endif()
