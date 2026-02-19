project(ndmanager-plugins-src-process_medianfilter)
cmake_minimum_required(VERSION 3.18)

set(program process_medianfilter)

# ── CUDA ──────────────────────────────────────────────────────────────────────
include(CheckLanguage)
check_language(CUDA)

if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    message(STATUS "process_medianfilter: CUDA found – building GPU-accelerated binary")

    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)

    # sm_120 = Blackwell (RTX 5070 Ti, requires CUDA 12.8+)
    # sm_89  = Ada Lovelace (RTX 4090 etc.)
    # sm_86  = Ampere (RTX 3090 etc.)
    set(CMAKE_CUDA_ARCHITECTURES 86 89 120 100)

    add_executable(${program}
        process_medianfilter.cpp
        process_medianfilter_cuda.cu)

    target_compile_definitions(${program} PRIVATE USE_CUDA)

    set_target_properties(${program} PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON)

    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${program} OpenMP::OpenMP_CXX)
    endif()

else()
    message(STATUS "process_medianfilter: CUDA not found – CPU-only (OpenMP) build")

    add_executable(${program} process_medianfilter.cpp)

    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${program} OpenMP::OpenMP_CXX)
        message(STATUS "process_medianfilter: OpenMP enabled")
    else()
        message(STATUS "process_medianfilter: OpenMP not found, single-threaded")
    endif()
endif()

target_compile_features(${program} PRIVATE cxx_std_17)
install(TARGETS ${program} DESTINATION bin)

# ── Man pages ─────────────────────────────────────────────────────────────────
set(docbooks process_medianfilter.docbook)
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
