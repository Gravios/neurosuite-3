project(ndmanager-plugins-src-xpathReader)
cmake_minimum_required(VERSION 3.16)

set(program xpathReader)
find_package(LibXml2 REQUIRED)
add_executable(${program} xpathReader.c)
target_link_libraries(${program} LibXml2::LibXml2)
install(TARGETS ${program} DESTINATION bin)
add_manpage(${program})
