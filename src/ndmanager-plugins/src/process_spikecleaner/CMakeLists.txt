add_executable(process_spikecleaner
    process_spikecleaner.cpp)
target_compile_features(process_spikecleaner PRIVATE cxx_std_17)
install(TARGETS process_spikecleaner DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_spikecleaner)
