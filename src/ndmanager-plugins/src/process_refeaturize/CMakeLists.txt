add_executable(process_refeaturize process_refeaturize.cpp)
target_compile_features(process_refeaturize PRIVATE cxx_std_17)
target_link_libraries(process_refeaturize PRIVATE m)
install(TARGETS process_refeaturize DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Opt into host-ISA vectorisation (-march=native) for this CPU-bound, hand-written
# numeric kernel.  ns_native_arch (cmake/ZenOptimizations.cmake) is CXX-only and
# gated on NS_ZEN_OPT, so device TUs are untouched and NS_ZEN_OPT=OFF stays
# portable.  Guarded so the plugin still builds if the module is absent.
if(TARGET ns_native_arch)
    target_link_libraries(process_refeaturize PRIVATE ns_native_arch)
endif()
