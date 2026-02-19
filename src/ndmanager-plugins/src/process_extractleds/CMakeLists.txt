project(ndmanager-plugins-src-process_extractleds)
cmake_minimum_required(VERSION 3.16)

# Find system FFmpeg libraries (replaces vendored libav-11.2)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswscale)

set(program process_extractleds)
add_executable(${program} process_extractleds.c)
target_include_directories(${program} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(${program} PRIVATE ${FFMPEG_LIBRARIES} m)
target_compile_options(${program} PRIVATE ${FFMPEG_CFLAGS_OTHER} -Wall -Wextra -O2)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/${program}" DESTINATION bin)

# Man page generation (unchanged)
set(docbooks process_extractleds.docbook)
file(GLOB_RECURSE files /usr/share/docbook.xsl)
foreach(file ${files})
    string(FIND ${file} manpages found)
    if(NOT found EQUAL -1)
        set(xsl ${file})
    endif()
endforeach()
set(manpages)
foreach(docbook ${docbooks})
    string(REGEX REPLACE docbook 1 manpage ${docbook})
    add_custom_command(OUTPUT ${manpage}
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${docbook}"
        COMMAND xsltproc ${xsl} "${CMAKE_CURRENT_SOURCE_DIR}/${docbook}")
    add_custom_command(OUTPUT ${manpage}.gz
        DEPENDS ${manpage}
        COMMAND gzip -f ${manpage})
    list(APPEND manpages ${manpage}.gz)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${manpage}.gz" DESTINATION man/man1)
endforeach()
add_custom_target(man-${program} ALL DEPENDS ${manpages})
