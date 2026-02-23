add_executable(process_merge process_merge.cpp)
target_compile_features(process_merge PRIVATE cxx_std_17)
install(TARGETS process_merge DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_merge)
