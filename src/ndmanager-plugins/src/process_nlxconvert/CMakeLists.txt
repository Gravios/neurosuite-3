add_executable(process_nlxconvert
    ncsfilecollection.cpp ncsfile.cpp nevfile.cpp
    nlxerror.cpp nlxextractor.cpp
    nvtfile.cpp outputfile.cpp process_nlxconvert.cpp
    smifilecollection.cpp smifile.cpp
    progressbar.cpp)
target_include_directories(process_nlxconvert
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(process_nlxconvert PRIVATE cxx_std_17)
install(TARGETS process_nlxconvert DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_nlxconvert)
