project(ndmanager-plugins-src-process_mergefeatures)
cmake_minimum_required(VERSION 3.16)

set(sources process_mergefeatures.cpp)
set(program process_mergefeatures)
add_executable(${program} ${sources})
target_compile_features(${program} PRIVATE cxx_std_11)
install(TARGETS ${program} DESTINATION bin)
