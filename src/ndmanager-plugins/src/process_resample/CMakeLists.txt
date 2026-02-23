project(ndmanager-plugins-src-process_resample)
cmake_minimum_required(VERSION 3.16)

set(source process_resample.cpp)
set(program process_resample)

# ── Prefer system libsamplerate; fall back to vendored build if not found ───
find_package(PkgConfig QUIET)
set(LSR_FOUND FALSE)

if(PkgConfig_FOUND)
    pkg_check_modules(LSR QUIET samplerate)
    if(LSR_FOUND)
        message(STATUS "process_resample: using system libsamplerate ${LSR_VERSION}")
    endif()
endif()

if(NOT LSR_FOUND)
    message(STATUS "process_resample: system libsamplerate not found, building vendored copy")
    set(lsr_source "${CMAKE_CURRENT_SOURCE_DIR}/libsamplerate-0.1.8")
    set(lsr_lib    "${CMAKE_CURRENT_BINARY_DIR}/lsr/src/.libs/libsamplerate.a")

    add_custom_command(OUTPUT lsr1
        COMMAND [ -d lsr ] || cp -a ${lsr_source} lsr
        COMMAND chmod +x lsr/configure lsr/autogen.sh lsr/Cfg/install-sh
                          lsr/Cfg/config.guess lsr/Cfg/config.sub lsr/Cfg/depcomp
                          lsr/Cfg/compile lsr/Cfg/ltmain.sh lsr/Cfg/missing
                          lsr/src/check_asm.sh
    )
    add_custom_command(OUTPUT lsr2
        COMMAND cd lsr && ./configure && make
        DEPENDS lsr1
    )
    add_custom_target(lsr DEPENDS lsr2)

    set(LSR_INCLUDE_DIRS "${lsr_source}/src")
    set(LSR_LIBRARIES    m ${lsr_lib})
    set(LSR_VENDORED     TRUE)
endif()

add_executable(${program} ${source})
target_compile_features(${program} PRIVATE cxx_std_17)
target_include_directories(${program} PRIVATE ${LSR_INCLUDE_DIRS})

if(DEFINED LSR_VENDORED)
    add_dependencies(${program} lsr)
endif()

# OpenMP for parallel per-channel resampling
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(${program} OpenMP::OpenMP_CXX ${LSR_LIBRARIES})
    message(STATUS "process_resample: OpenMP enabled")
else()
    target_link_libraries(${program} ${LSR_LIBRARIES})
    message(STATUS "process_resample: OpenMP not found, single-threaded build")
endif()

install(TARGETS ${program} DESTINATION bin)

# Man pages
set(docbooks process_resample.docbook)
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
        COMMAND xsltproc ${xsl} "${CMAKE_CURRENT_SOURCE_DIR}/${docbook}"
    )
    add_custom_command(OUTPUT ${manpage}.gz
        DEPENDS ${manpage}
        COMMAND gzip -f ${manpage}
    )
    list(APPEND manpages ${manpage}.gz)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${manpage}.gz" DESTINATION man/man1)
endforeach()
add_custom_target(man-${program} ALL DEPENDS ${manpages})
