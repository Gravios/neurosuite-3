project(ndmanager-plugins-descriptions)
cmake_minimum_required(VERSION 3.16)

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/*.xml")
install(FILES ${files} DESTINATION share/ndmanager/descriptions)
