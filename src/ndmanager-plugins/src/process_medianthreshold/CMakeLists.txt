project(ndmanager-plugins-src-process_medianthreshold)
cmake_minimum_required(VERSION 3.18)

set(program process_medianthreshold)

# ── CUDA ──────────────────────────────────────────────────────────────────────
include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    message(STATUS "process_medianthreshold: CUDA found – building GPU-accelerated binary")

    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(CMAKE_CUDA_ARCHITECTURES 86 89 120 100)

    add_executable(${program}
        process_medianthreshold.cpp
        process_medianthreshold_cuda.cu)

    target_compile_definitions(${program} PRIVATE USE_CUDA)

    set_target_properties(${program} PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON)

else()
    message(STATUS "process_medianthreshold: CUDA not found – CPU-only build")
    add_executable(${program} process_medianthreshold.cpp)
endif()

target_compile_features(${program} PRIVATE cxx_std_17)
install(TARGETS ${program} DESTINATION bin)

# ── Man pages ─────────────────────────────────────────────────────────────────
set(docbooks process_medianthreshold.docbook)
file(GLOB_RECURSE xsl_files /usr/share/docbook.xsl)
foreach(f ${xsl_files})
    string(FIND ${f} manpages found)
    if(NOT found EQUAL -1)
        set(xsl ${f})
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
