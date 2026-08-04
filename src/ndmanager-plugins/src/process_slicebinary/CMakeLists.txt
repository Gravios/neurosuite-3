add_executable(process_slicebinary process_slicebinary.cpp)
target_compile_features(process_slicebinary PRIVATE cxx_std_17)
install(TARGETS process_slicebinary DESTINATION "${CMAKE_INSTALL_BINDIR}")
