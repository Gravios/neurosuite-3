# Path to the canonical klustersshared headers and the progressbar
# implementation.  We reference the source files directly rather than
# linking libklustersshared because process_nlxconvert is a pure CLI
# tool with no Qt dependency, and libklustersshared is a Qt library —
# linking it would drag in Qt at runtime for a tool that doesn't need it.
set(_klustersshared_dir
    "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")

add_executable(process_nlxconvert
    ncsfilecollection.cpp ncsfile.cpp nevfile.cpp
    nlxerror.cpp nlxextractor.cpp
    nvtfile.cpp outputfile.cpp process_nlxconvert.cpp
    smifilecollection.cpp smifile.cpp
    "${_klustersshared_dir}/progressbar.cpp")
target_include_directories(process_nlxconvert
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${_klustersshared_dir}")
target_compile_features(process_nlxconvert PRIVATE cxx_std_17)
install(TARGETS process_nlxconvert DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_nlxconvert)
