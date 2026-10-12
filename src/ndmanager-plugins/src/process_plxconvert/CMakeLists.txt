# process_plxconvert — Plexon .plx / .pl2 → .dat + metadata YAML
#
# No external dependencies (C++20 standard library only).

add_executable(process_plxconvert process_plxconvert.cpp)

target_compile_features(process_plxconvert PRIVATE cxx_std_20)

target_compile_options(process_plxconvert PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2>)

install(TARGETS process_plxconvert
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
