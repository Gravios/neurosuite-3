cmake_minimum_required(VERSION 3.16)

add_executable(process_refeaturize_stderiv
    process_refeaturize_stderiv.cpp
)
target_compile_features(process_refeaturize_stderiv PRIVATE cxx_std_17)

install(TARGETS process_refeaturize_stderiv
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
add_manpage(process_refeaturize_stderiv)

# Opt into host-ISA vectorisation (-march=native) for this CPU-bound, hand-written
# numeric kernel.  ns_native_arch (cmake/ZenOptimizations.cmake) is CXX-only and
# gated on NS_ZEN_OPT, so device TUs are untouched and NS_ZEN_OPT=OFF stays
# portable.  Guarded so the plugin still builds if the module is absent.
if(TARGET ns_native_arch)
    target_link_libraries(process_refeaturize_stderiv PRIVATE ns_native_arch)
endif()
