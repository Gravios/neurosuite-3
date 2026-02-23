project(ndmanager-plugins-src-process_extractleds)
cmake_minimum_required(VERSION 3.16)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec libavformat libavutil libswscale)

set(program process_extractleds)
add_executable(${program} process_extractleds.c)
target_include_directories(${program} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(${program} PRIVATE ${FFMPEG_LIBRARIES} m)
target_compile_options(${program} PRIVATE ${FFMPEG_CFLAGS_OTHER} -Wall -Wextra -O2)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/${program}" DESTINATION bin)
add_manpage(${program})
