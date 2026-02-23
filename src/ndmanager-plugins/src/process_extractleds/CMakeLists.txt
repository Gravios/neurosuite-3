find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)

add_executable(process_extractleds process_extractleds.c)
target_compile_options(process_extractleds PRIVATE
    ${FFMPEG_CFLAGS_OTHER})
target_include_directories(process_extractleds PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(process_extractleds PRIVATE ${FFMPEG_LINK_LIBRARIES} m)
install(TARGETS process_extractleds DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_extractleds)
