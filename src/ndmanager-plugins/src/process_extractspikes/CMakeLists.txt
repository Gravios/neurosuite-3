project(ndmanager-plugins-src-process_extractspikes)
cmake_minimum_required(VERSION 3.16)

set(sources process_extractspikes.cpp)
set(program process_extractspikes)
add_executable(${program} ${sources})
target_compile_features(${program} PRIVATE cxx_std_17)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(${program} OpenMP::OpenMP_CXX)
    message(STATUS "process_extractspikes: OpenMP enabled")
else()
    message(STATUS "process_extractspikes: OpenMP not found, single-threaded build")
endif()

install(TARGETS ${program} DESTINATION bin)

# Man pages
set(docbooks process_extractspikes.docbook)
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
        DEPENDS ${manpage} COMMAND gzip -f ${manpage})
    list(APPEND manpages ${manpage}.gz)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${manpage}.gz" DESTINATION man/man1)
endforeach()
add_custom_target(man-${program} ALL DEPENDS ${manpages})
