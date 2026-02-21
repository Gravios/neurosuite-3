project(ndmanager-plugins-src-process_refeaturize)
cmake_minimum_required(VERSION 3.16)

set(sources process_refeaturize.cpp)
set(program process_refeaturize)
add_executable(${program} ${sources})
target_link_libraries(${program} m)
install(TARGETS ${program} DESTINATION bin)
