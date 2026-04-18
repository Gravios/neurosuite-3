# process_decomposecollisions — CMakeLists.txt
# Collision decomposition plugin with optional CUDA GPU acceleration.
# CPU-only binary:  process_decomposecollisions_cpu   (always built)
# GPU binary:       process_decomposecollisions        (when CUDA available)
#
# To disable CUDA:  cmake -DPDC_USE_CUDA=OFF ...
# To set archs:     cmake -DPDC_CUDA_ARCHS="86;89" ...

option(PDC_USE_CUDA "Build process_decomposecollisions with CUDA" ON)
set(PDC_CUDA_ARCHS "86;89;100;120"
    CACHE STRING "CUDA architectures for process_decomposecollisions")

set(_srcs process_decomposecollisions.cpp)
set(_hdr  process_decomposecollisions.h)

# ── Common compile options ─────────────────────────────────────────────────
add_library(_pdc_flags INTERFACE)
target_compile_features(_pdc_flags INTERFACE cxx_std_20)
target_compile_options(_pdc_flags INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -Wall -Wextra>)

find_package(OpenMP)

# ── CPU binary (always built) ─────────────────────────────────────────────
add_executable(process_decomposecollisions_cpu ${_srcs} ${_hdr})
target_link_libraries(process_decomposecollisions_cpu PRIVATE _pdc_flags)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_decomposecollisions_cpu PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_decomposecollisions_cpu: OpenMP enabled")
endif()
install(TARGETS process_decomposecollisions_cpu
        DESTINATION "${CMAKE_INSTALL_BINDIR}")

# ── CUDA binary ───────────────────────────────────────────────────────────
include(CheckLanguage)
set(_CUDA_ACTIVE FALSE)
if(PDC_USE_CUDA)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(_CUDA_ACTIVE TRUE)
        message(STATUS "process_decomposecollisions: CUDA available — ${CMAKE_CUDA_COMPILER}")
    else()
        message(STATUS "process_decomposecollisions: CUDA requested but nvcc not found")
    endif()
endif()

if(_CUDA_ACTIVE)
    find_package(CUDAToolkit REQUIRED)

    add_executable(process_decomposecollisions ${_srcs} ${_hdr})
    set_target_properties(process_decomposecollisions PROPERTIES
        CUDA_SEPARABLE_COMPILATION OFF)
    target_compile_features(process_decomposecollisions PRIVATE cxx_std_20)
    target_compile_options(process_decomposecollisions PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -Wall -Wextra>>
        $<$<COMPILE_LANGUAGE:CUDA>:
            -O3 --use_fast_math -Xptxas -v>)
    set_property(TARGET process_decomposecollisions
                 PROPERTY CUDA_ARCHITECTURES "${CUDA_ARCHS}")
    target_compile_definitions(process_decomposecollisions PRIVATE
        USE_CUDA
        $<$<CONFIG:Release,RelWithDebInfo>:NDEBUG>)
    target_link_libraries(process_decomposecollisions PRIVATE
        CUDA::cudart CUDA::cublas)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(process_decomposecollisions PRIVATE OpenMP::OpenMP_CXX)
    endif()
    install(TARGETS process_decomposecollisions
            DESTINATION "${CMAKE_INSTALL_BINDIR}")

    message(STATUS "  CUDA architectures : ${PDC_CUDA_ARCHS}")
    message(STATUS "  CUDA toolkit       : ${CUDAToolkit_VERSION}")
else()
    # Symlink/alias so the shell script can call "process_decomposecollisions"
    # and get the CPU build on machines without CUDA.
    install(CODE "
        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
            process_decomposecollisions_cpu
            \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/process_decomposecollisions\")
    ")
    message(STATUS "process_decomposecollisions: CPU-only build; GPU binary not created")
endif()
