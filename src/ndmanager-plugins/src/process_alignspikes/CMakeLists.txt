# sdiff_pairs.h (shared SDIFF_CUSTOM pattern parser) is header-only; take it
# as an include dir rather than linking the Qt libklustersshared, exactly as
# process_extractspikes_stderiv does.
set(_klustersshared_dir
    "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")
add_executable(process_alignspikes process_alignspikes.cpp)
target_include_directories(process_alignspikes PRIVATE "${_klustersshared_dir}")
target_compile_features(process_alignspikes PRIVATE cxx_std_17)
target_link_libraries(process_alignspikes PRIVATE m)
install(TARGETS process_alignspikes DESTINATION "${CMAKE_INSTALL_BINDIR}")
