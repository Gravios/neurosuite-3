# Prefer system libsamplerate; fall back to vendored 0.1.8 if absent.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LSR QUIET samplerate)
endif()

if(LSR_FOUND)
    message(STATUS "process_resample: system libsamplerate ${LSR_VERSION}")
    # Create an IMPORTED target so we link consistently with the other targets
    add_library(samplerate::samplerate INTERFACE IMPORTED)
    target_include_directories(samplerate::samplerate INTERFACE ${LSR_INCLUDE_DIRS})
    target_link_libraries(samplerate::samplerate INTERFACE ${LSR_LINK_LIBRARIES})
    set(_lsr_target samplerate::samplerate)
else()
    message(STATUS "process_resample: building vendored libsamplerate 0.1.8")
    set(_lsr_src "${CMAKE_CURRENT_SOURCE_DIR}/libsamplerate-0.1.8")
    set(_lsr_lib "${CMAKE_CURRENT_BINARY_DIR}/lsr/src/.libs/libsamplerate.a")

    add_custom_command(
        OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/lsr_unpacked"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_lsr_src}"
                "${CMAKE_CURRENT_BINARY_DIR}/lsr"
        COMMAND chmod +x
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/configure"
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/Cfg/install-sh"
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/Cfg/config.guess"
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/Cfg/config.sub"
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/Cfg/depcomp"
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/Cfg/compile"
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/Cfg/ltmain.sh"
            "${CMAKE_CURRENT_BINARY_DIR}/lsr/Cfg/missing"
        COMMAND "${CMAKE_COMMAND}" -E touch
                "${CMAKE_CURRENT_BINARY_DIR}/lsr_unpacked"
        COMMENT "Unpacking vendored libsamplerate"
        VERBATIM)

    # The vendored libsamplerate is built by its own autotools ./configure, which
    # the top-level add_compile_options() (where NS_NATIVE_ARCH lives) does not
    # reach — so honour the opt-out here explicitly.  Default to native when the
    # option is undefined (e.g. a standalone plugin configure).
    if(DEFINED NS_NATIVE_ARCH AND NOT NS_NATIVE_ARCH)
        set(_lsr_march "")            # portable build requested
    else()
        set(_lsr_march "-march=native ")
    endif()

    add_custom_command(
        OUTPUT  "${_lsr_lib}"
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/lsr_unpacked"
        COMMAND cd "${CMAKE_CURRENT_BINARY_DIR}/lsr" &&
                ./configure
                "CFLAGS=-O3 ${_lsr_march}-ffast-math -funroll-loops -Wno-implicit-fallthrough -Wno-absolute-value -Wno-shift-negative-value"
                && make -j
        COMMENT "Building vendored libsamplerate (optimized)"
        VERBATIM)

    add_custom_target(vendored_lsr DEPENDS "${_lsr_lib}")

    add_library(samplerate::samplerate INTERFACE IMPORTED)
    target_include_directories(samplerate::samplerate
        INTERFACE "${_lsr_src}/src")
    target_link_libraries(samplerate::samplerate
        INTERFACE m "${_lsr_lib}")
    set(_lsr_target samplerate::samplerate)
endif()

find_package(OpenMP)

add_executable(process_resample process_resample.cpp)
target_compile_features(process_resample PRIVATE cxx_std_17)
target_compile_options(process_resample PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -O3 -ffast-math -funroll-loops>)
# V-Cache-aware chunk size for AMD Ryzen 9800X3D (96 MB 3D V-Cache).
# At 64 channels each float32 chunk = chunk_frames × 64 × 4 bytes.
# 131072 frames × 64 ch × 4 bytes = 32 MB  (fits two chunks + state in 96 MB).
# Override on the command line: cmake -DPROCESS_RESAMPLE_CHUNK_FRAMES=N ...
if(NOT DEFINED PROCESS_RESAMPLE_CHUNK_FRAMES)
    set(PROCESS_RESAMPLE_CHUNK_FRAMES 131072)
endif()
target_compile_definitions(process_resample PRIVATE
    CHUNK_FRAMES=${PROCESS_RESAMPLE_CHUNK_FRAMES})
target_link_libraries(process_resample PRIVATE ${_lsr_target})
if(NOT LSR_FOUND)
    add_dependencies(process_resample vendored_lsr)
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_resample PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_resample: OpenMP enabled")
else()
    message(STATUS "process_resample: OpenMP not found, single-threaded")
endif()
install(TARGETS process_resample DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_resample)
