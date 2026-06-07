find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale)
endif()
if(NOT FFMPEG_FOUND)
    message(STATUS "process_extractleds: FFmpeg (libavcodec/libavformat/libavutil/"
                   "libswscale) not found — skipping this plugin. Install the FFmpeg "
                   "development packages to enable LED-from-video extraction.")
    return()
endif()

add_executable(process_extractleds process_extractleds.c)
target_compile_options(process_extractleds PRIVATE
    ${FFMPEG_CFLAGS_OTHER})
target_include_directories(process_extractleds PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(process_extractleds PRIVATE ${FFMPEG_LINK_LIBRARIES} m)
install(TARGETS process_extractleds DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_extractleds)
