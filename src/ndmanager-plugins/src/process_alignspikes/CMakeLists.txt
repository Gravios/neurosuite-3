# sdiff_pairs.h (shared SDIFF_CUSTOM pattern parser) is header-only; take it
# as an include dir rather than linking the Qt libklustersshared, exactly as
# process_extractspikes_stderiv does.
set(_klustersshared_dir
    "${NDM_NS3_ROOT}/src/libklustersshared/src/klustersshared")
add_executable(process_alignspikes process_alignspikes.cpp)
target_include_directories(process_alignspikes PRIVATE "${_klustersshared_dir}")
target_compile_features(process_alignspikes PRIVATE cxx_std_20)
# libm: a separate library on Unix; part of the C runtime on Windows.
target_link_libraries(process_alignspikes PRIVATE $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
install(TARGETS process_alignspikes DESTINATION "${CMAKE_INSTALL_BINDIR}")
