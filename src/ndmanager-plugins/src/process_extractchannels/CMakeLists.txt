project(ndmanager-plugins-src-process_extractchannels)
cmake_minimum_required(VERSION 3.16)

set(program process_extractchannels)
add_executable(${program} process_extractchannels.cpp)
target_compile_features(${program} PRIVATE cxx_std_17)
install(TARGETS ${program} DESTINATION bin)
add_manpage(${program})
