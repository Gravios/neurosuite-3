add_executable(process_alignspikes process_alignspikes.cpp)
target_compile_features(process_alignspikes PRIVATE cxx_std_17)
target_link_libraries(process_alignspikes PRIVATE m)
install(TARGETS process_alignspikes DESTINATION "${CMAKE_INSTALL_BINDIR}")
