add_executable(process_refeaturize process_refeaturize.cpp)
target_compile_features(process_refeaturize PRIVATE cxx_std_17)
target_link_libraries(process_refeaturize PRIVATE m)
install(TARGETS process_refeaturize DESTINATION "${CMAKE_INSTALL_BINDIR}")
