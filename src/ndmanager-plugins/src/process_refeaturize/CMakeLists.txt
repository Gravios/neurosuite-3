add_executable(process_refeaturize process_refeaturize.cpp)
target_compile_features(process_refeaturize PRIVATE cxx_std_20)
# libm: a separate library on Unix; part of the C runtime on Windows.
target_link_libraries(process_refeaturize PRIVATE $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
install(TARGETS process_refeaturize DESTINATION "${CMAKE_INSTALL_BINDIR}")
