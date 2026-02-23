add_executable(process_mergefeatures process_mergefeatures.cpp)
target_compile_features(process_mergefeatures PRIVATE cxx_std_17)
install(TARGETS process_mergefeatures DESTINATION "${CMAKE_INSTALL_BINDIR}")
