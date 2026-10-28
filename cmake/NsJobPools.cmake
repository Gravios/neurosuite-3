# =============================================================================
# NsJobPools.cmake
#
# Keep a full build inside the machine's memory.
#
# One Klusters translation unit takes 0.7-0.9 GiB to compile with GCC 15, Qt 6
# and LTO (measured on Ubuntu 26.04: clusterview.cpp 748 MiB, klusters.cpp
# 936 MiB), and Klusters alone has 122 of them.  When more jobs run than the
# memory holds, the machine swaps until nothing else gets scheduled: the
# desktop session (ThinLinc, X) and sshd stop answering and the box has to be
# reset.  The last lines Make prints before that are the "Building CUDA
# object" ones, because the .cu files come last in klusters' source list, but
# they are not what fills the memory (each .cu peaks at 0.2-0.6 GiB).
#
# Ninja: the compile and link steps of every target go into two job pools
# whose depth comes from the installed memory, so `cmake --build build
# --parallel` or `ninja -j 64` can never start more than fits.
#
# Makefiles: a pool cannot be expressed, and `cmake --build build --parallel`
# with no number becomes `make -j` with NO limit (checked with CMake 4.2: the
# make command line is `-f Makefile -j`).  Configure warns and prints a job
# count that fits.
#
# Options
#   NS_JOB_POOLS          ON   use the pools (Ninja only)
#   NS_COMPILE_JOB_MIB    1536 memory budgeted per compile job (MiB)
#   NS_LINK_JOB_MIB       4096 memory budgeted per link job (MiB); an LTO link
#                              of klusters is the largest single step
# Included from the top-level CMakeLists.txt before any add_subdirectory(), so
# every target is created with CMAKE_JOB_POOL_COMPILE / _LINK already set.
# =============================================================================

option(NS_JOB_POOLS "Limit parallel compile and link jobs to the installed memory (Ninja)" ON)
set(NS_COMPILE_JOB_MIB 1536 CACHE STRING "Memory budgeted per compile job (MiB)")
set(NS_LINK_JOB_MIB    4096 CACHE STRING "Memory budgeted per link job (MiB)")

cmake_host_system_information(RESULT _ns_mem_mib QUERY TOTAL_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT _ns_cores   QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT _ns_cores OR _ns_cores LESS 1)
    set(_ns_cores 1)
endif()

# ns_jobs_for(<out> <MiB per job>): jobs that fit, between 1 and the core count.
function(ns_jobs_for _out _per_job)
    if(NOT _ns_mem_mib OR NOT _per_job OR _per_job LESS 1)
        set(${_out} ${_ns_cores} PARENT_SCOPE)
        return()
    endif()
    math(EXPR _n "${_ns_mem_mib} / ${_per_job}")
    if(_n LESS 1)
        set(_n 1)
    endif()
    if(_n GREATER _ns_cores)
        set(_n ${_ns_cores})
    endif()
    set(${_out} ${_n} PARENT_SCOPE)
endfunction()

ns_jobs_for(_ns_compile_jobs ${NS_COMPILE_JOB_MIB})
ns_jobs_for(_ns_link_jobs    ${NS_LINK_JOB_MIB})

if(CMAKE_GENERATOR MATCHES "Ninja")
    if(NS_JOB_POOLS)
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS
            ns_compile=${_ns_compile_jobs}
            ns_link=${_ns_link_jobs})
        set(CMAKE_JOB_POOL_COMPILE ns_compile)
        set(CMAKE_JOB_POOL_LINK    ns_link)
        message(STATUS "neurosuite-3: job pools ${_ns_compile_jobs} compile / "
                       "${_ns_link_jobs} link (${_ns_mem_mib} MiB, ${_ns_cores} cores; "
                       "NS_COMPILE_JOB_MIB=${NS_COMPILE_JOB_MIB}, NS_LINK_JOB_MIB=${NS_LINK_JOB_MIB})")
    endif()
elseif(CMAKE_GENERATOR MATCHES "Makefiles")
    message(WARNING
        "neurosuite-3: the ${CMAKE_GENERATOR} generator cannot limit parallel jobs "
        "to the memory.  `cmake --build <dir> --parallel` without a number runs "
        "`make -j` with no limit, which on this tree asks for far more memory than "
        "the machine has and can make it stop responding (desktop and ssh).\n"
        "Build with a job count that fits ${_ns_mem_mib} MiB:\n"
        "    cmake --build <dir> -j ${_ns_compile_jobs}\n"
        "or configure a new build directory with Ninja, which applies the limit "
        "itself:\n"
        "    cmake -B <dir> -G Ninja ...")
endif()

unset(_ns_mem_mib)
unset(_ns_cores)
unset(_ns_compile_jobs)
unset(_ns_link_jobs)
