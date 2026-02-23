project(ndmanager-plugins-src-process_merge)
cmake_minimum_required(VERSION 3.16)

set(program process_merge)
add_executable(${program} process_merge.cpp)
target_compile_features(${program} PRIVATE cxx_std_17)
install(TARGETS ${program} DESTINATION bin)
add_manpage(${program})
